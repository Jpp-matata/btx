// Copyright (c) 2026 The BTX developers. MIT license.
// All identities, credentials and model tensors below are synthetic fixtures.
// Two signer identities on this test host do NOT qualify two physical hosts.
#include <modelnet/security_v2/service.h>
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <thread>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif
using namespace modelnet::security;
namespace {
struct Harness {
    size_t passed{0},failed{0};Json results=Array();
    template<class F> void Test(const std::string& name,F f) {
        Json row=Object();row.pushKV("name",name);
        try{f();++passed;row.pushKV("status","PASS");std::cout<<"PASS "<<name<<'\n';}
        catch(const std::exception& e){++failed;row.pushKV("status","FAIL");row.pushKV("error",e.what());std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}
        results.push_back(row);
    }
};
void Check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
template<class F> void Reject(F f,std::string_view expected={}) {
    try{f();}catch(const Error& e){if(!expected.empty()&&e.code!=expected)throw std::runtime_error("expected "+std::string(expected)+", received "+e.code);return;}
    throw std::runtime_error("expected refusal");
}
struct Temp {
    fs::path root;
    Temp(){root=fs::canonical(fs::temp_directory_path())/("btx-security-test-"+Hex(RandomBytes(12)));EnsurePrivateDirectory(root);}
    ~Temp(){std::error_code ec;fs::remove_all(root,ec);}
};
const std::vector<std::string> GATES{"external-controls","outcome","revocation","rollback","privacy","adversarial"};
Json Signed(const std::string& kind,const Json& body,const KeyPair& a,const KeyPair* b=nullptr) {
    auto e=Envelope(kind,body);AddSignature(e,a);if(b)AddSignature(e,*b);return e;
}
Json Common(const std::string& realm,uint64_t epoch,int64_t now,uint64_t ttl=60000) {
    auto b=Object();b.pushKV("realm",realm);b.pushKV("policy_epoch",epoch);b.pushKV("issued_ms",now);b.pushKV("expires_ms",static_cast<uint64_t>(now)+ttl);return b;
}
struct Fixture {
    Temp temp;fs::path root;Database db;
    KeyPair owner=GenerateKey(),publisher=GenerateKey(),second=GenerateKey(),alice=GenerateKey(),bob=GenerateKey();
    std::string realm=Hash("test realm"),session=Hex(RandomBytes(32)),runtime=Hash("test runtime"),schema=Hash("tool schema");
    int64_t now=NowMs();Json policy_record,local;
    Policy policy;
    fs::path probe;
    std::string capsule,composition;std::vector<std::string> evaluations;
    explicit Fixture(fs::path executable={},bool common_domain=false):root(temp.root/"security"),db(root),probe(std::move(executable)) {
        if(!probe.empty())runtime=Hash(ReadFile(probe,128*1024*1024));
        auto pins=Object();pins.pushKV("schema","BTX/SecurityRootPins/v1");pins.pushKV("keys",Strings({KeyId(owner.public_key)}));pins.pushKV("threshold",1);WritePrivate(root/"root-pins.json",Canonical(pins));
        Json b=Object();b.pushKV("realm",realm);b.pushKV("epoch",1);b.pushKV("issued_ms",now);b.pushKV("expires_ms",now+3600000);
        b.pushKV("release_threshold",2);b.pushKV("evaluator_domains",2);b.pushKV("physical_hosts",2);b.pushKV("evidence_ttl_ms",60000);
        b.pushKV("canary_ms",1000);b.pushKV("trial_limit",8);b.pushKV("gates",Strings(GATES));Json principals=Array();
        auto principal=[&](const KeyPair& key,std::vector<std::string> roles,std::string domain,std::string host,std::string pin="") {
            Json p=Object();p.pushKV("public_key",Hex(key.public_key));p.pushKV("roles",Strings(roles));p.pushKV("domain",domain);p.pushKV("host",host);p.pushKV("transport_pin",pin);principals.push_back(p);
        };
        principal(owner,{"owner","release","revoker","coordinator","witness"},"owner","owner-host");
        principal(publisher,{"publisher"},"author","publisher-host");principal(second,{"release","revoker"},"release","release-host");
        principal(alice,{"evaluator","worker","peer"},"eval-a","host-a",Hash("pin-a"));
        principal(bob,{"evaluator","worker","peer"},common_domain?"eval-a":"eval-b","host-b",Hash("pin-b"));
        b.pushKV("principals",principals);policy_record=Signed("policy",b,owner);InstallPolicy(db,policy_record,now);policy=Policy::Load(db,now);
        EnsurePrivateDirectory(temp.root/"workspace");WritePrivate(temp.root/"workspace"/"source.txt","hello safe workspace\n");
        WritePrivate(temp.root/"workspace"/".env","SYNTHETIC_DECOY=not-a-real-secret\n");
        local=Object();local.pushKV("schema","BTX/SecurityLocal/v1");local.pushKV("host","host-a");auto work=Object();work.pushKV("code",(temp.root/"workspace").string());local.pushKV("workspaces",work);
        local.pushKV("packages",Object());local.pushKV("evaluation_plans",Object());local.pushKV("runtimes",Object());
        auto identity=Object();identity.pushKV("public_key",Hex(alice.public_key));identity.pushKV("secret_file",(root/"worker.key").string());local.pushKV("identity",identity);WritePrivate(root/"worker.key",Hex(alice.secret_key));
        auto compute=Object();compute.pushKV("allow_evaluation",true);compute.pushKV("max_memory_bytes",512*1024*1024);compute.pushKV("max_wall_ms",5000);compute.pushKV("max_cpu_ms",4000);local.pushKV("execution_policy",compute);SaveLocal();
        composition=MakeComposition("base");capsule=MakeEvidence(composition);
    }
    void SaveLocal(){WritePrivate(root/"local.json",Canonical(local),true);}
    std::string Import(const Json& e){ImportRecord(db,policy,e,NowMs());return RecordId(e);}
    Json Context(const std::string& c={}) {auto x=Object();x.pushKV("realm",realm);x.pushKV("composition",c.empty()?composition:c);x.pushKV("runtime",runtime);x.pushKV("tool_schema",schema);x.pushKV("session",session);return x;}
    std::string MakeComposition(const std::string& version,const std::vector<std::string>& parents={}) {
        auto b=Object();b.pushKV("realm",realm);b.pushKV("policy_epoch",1);b.pushKV("runtime",runtime);b.pushKV("sandbox",StaticSandboxDigest());b.pushKV("tool_schema",schema);b.pushKV("system_prompt",Hash("fixed system prompt"));b.pushKV("parents",Strings(parents));b.pushKV("contributors",Strings({KeyId(publisher.public_key)}));b.pushKV("license","MIT-test-fixture");b.pushKV("architecture","synthetic-fixture-not-an-LLM");b.pushKV("visibility","public");
        auto files=Array();const auto source=temp.root/("source-"+version);EnsurePrivateDirectory(source);
        const std::vector<std::pair<std::string,std::string>> components{{"weights.safetensors","weights"},{"tokenizer.json","tokenizer"},{"config.json","config"},{"chat.txt","chat_template"}};
        for(const auto& [path,role]:components) {
            std::string bytes=role=="weights"?std::string("synthetic tensors ")+version:std::string("{} ")+version;
            WritePrivate(source/path,bytes);auto f=Object();f.pushKV("path",path);f.pushKV("role",role);f.pushKV("sha384",Hash(bytes));f.pushKV("bytes",static_cast<uint64_t>(bytes.size()));files.push_back(f);
        }
        b.pushKV("files",files);auto e=Signed("composition",b,publisher);auto id=Import(e);ImportResident(db,policy,id,source,1024*1024,NowMs());return id;
    }
    Json Challenge(const std::string& c,const std::string& gate,std::string package={}) {
        auto b=Common(realm,1,now);b.pushKV("composition",c);b.pushKV("campaign",Hash("campaign "+gate));b.pushKV("suite",Hash("suite "+gate));b.pushKV("sample_commitment",Hash("samples "+gate));b.pushKV("package",package.empty()?Hash("test package "+gate):package);b.pushKV("gate",gate);b.pushKV("trial_limit",8);return Signed("challenge",b,owner);
    }
    Json Receipt(const std::string& c,const std::string& request,const std::string& package,
                 const KeyPair& key,const std::string& host,int64_t timestamp,std::string output=Hash("output")) {
        auto b=Common(realm,1,timestamp);b.pushKV("job",Hash(Hex(RandomBytes(16))));b.pushKV("request",request);b.pushKV("package",package);b.pushKV("composition",c);b.pushKV("runtime",runtime);b.pushKV("sandbox",StaticSandboxDigest());b.pushKV("outcome","PASS");b.pushKV("output_digest",output);b.pushKV("output_bytes",100);b.pushKV("host",host);b.pushKV("limits",Object());b.pushKV("dispatched",true);return Signed("receipt",b,key);
    }
    Json Evaluation(const Json& challenge,const KeyPair& key,const std::string& host,std::string outcome="PASS") {
        const auto& ch=challenge["body"];const auto challenge_id=RecordId(challenge);
        const auto receipt=Receipt(Text(ch,"composition"),challenge_id,Text(ch,"package"),key,host,now);
        auto receipt_id=Import(receipt);auto b=Common(realm,1,now);
        for(auto field:{"composition","campaign","suite","sample_commitment","gate"})b.pushKV(field,ch[field]);
        b.pushKV("challenge",challenge_id);b.pushKV("runtime",runtime);b.pushKV("sandbox",StaticSandboxDigest());b.pushKV("outcome",outcome);b.pushKV("cases",50);b.pushKV("execution_receipt",receipt_id);b.pushKV("output_digest",receipt["body"]["output_digest"]);b.pushKV("host",host);b.pushKV("physical_host_evidence",host);b.pushKV("limitations",Strings({"SYNTHETIC_ATTESTATION_FIXTURE_ONE_PHYSICAL_TEST_HOST"}));return Signed("evaluation",b,key);
    }
    std::string Capsule(const std::string& c,const std::vector<std::string>& reports) {
        auto b=Common(realm,1,now);b.pushKV("composition",c);b.pushKV("evaluations",Strings(reports));b.pushKV("profile","restricted-tools");return Import(Signed("capsule",b,owner,&second));
    }
    std::string MakeEvidence(const std::string& c) {
        std::vector<std::string> reports;
        for(const auto& gate:GATES){auto ch=Challenge(c,gate);Import(ch);reports.push_back(Import(Evaluation(ch,alice,"host-a")));reports.push_back(Import(Evaluation(ch,bob,"host-b")));}
        evaluations=reports;return Capsule(c,reports);
    }
    std::string Grant(unsigned calls=4) {
        auto b=Common(realm,1,now);b.pushKV("composition",composition);b.pushKV("channel","");b.pushKV("capsule",capsule);b.pushKV("runtime",runtime);b.pushKV("tool_schema",schema);b.pushKV("session",session);b.pushKV("workspace","code");b.pushKV("tools",Strings({"read","hash","write-new","run-static"}));b.pushKV("read_paths",Strings({"source.txt","link.txt"}));b.pushKV("write_paths",Strings({"new.txt"}));std::vector<std::string> packages;for(const auto& x:local["packages"].getKeys())packages.push_back(x);b.pushKV("packages",Strings(packages));b.pushKV("max_calls",calls);b.pushKV("max_bytes",262144);b.pushKV("per_action_bytes",65536);return Import(Signed("grant",b,owner));
    }
    void Revoke(const std::string& id){auto b=Common(realm,1,NowMs());b.pushKV("subject",id);b.pushKV("sequence",1);b.pushKV("reason","test incident");auto e=Signed("revocation",b,owner,&second);ImportRecord(db,Policy::Load(db,NowMs()),e,NowMs());}
    Json ReadRequest(){auto r=Object();r.pushKV("tool","read");r.pushKV("path","source.txt");return r;}
    std::string InstallProbePackage(){auto p=Object();p.pushKV("executable",probe.string());p.pushKV("sha384",Hash(ReadFile(probe,128*1024*1024)));p.pushKV("sandbox",StaticSandboxDigest());p.pushKV("wall_ms",1000);p.pushKV("cpu_ms",1000);p.pushKV("memory_bytes",256*1024*1024);const auto id=DomainHash("BTX/LocalStaticPackage/v1",Canonical(p));auto packages=local["packages"];packages.pushKV(id,p);local.pushKV("packages",packages);SaveLocal();return id;}
    Json FollowMandate(std::string runtime_id={}){auto b=Common(realm,1,now);b.pushKV("channel","coding/stable");b.pushKV("profile","restricted-tools");b.pushKV("runtimes",Strings({runtime_id.empty()?runtime:runtime_id}));b.pushKV("tool_schemas",Strings({schema}));b.pushKV("automatic_follow",true);return Signed("mandate",b,owner);}
    Json Channel(unsigned seq,const std::string& cap,std::string previous=""){auto b=Common(realm,1,now);b.pushKV("name","coding/stable");b.pushKV("sequence",seq);b.pushKV("capsule",cap);b.pushKV("previous",previous);return Signed("channel",b,owner,&second);}
};
std::string ExpectedProbe(){return "{\"filesystem_denied\":true,\"network_denied\":true,\"fork_denied\":true,\"environment_clean\":true}\n";}
std::string InstallPlan(Fixture& f,const std::string& gate="adversarial") {
    auto plan=Object();plan.pushKV("schema","BTX/StaticEvaluationPlan/v1");plan.pushKV("suite",Hash("suite "+gate));plan.pushKV("sample_commitment",Hash("samples "+gate));plan.pushKV("gate",gate);plan.pushKV("executable_sha384",Hash(ReadFile(f.probe,128*1024*1024)));plan.pushKV("sandbox",StaticSandboxDigest());plan.pushKV("input_sha384",Hash("probe"));plan.pushKV("expected_output_sha384",Hash(ExpectedProbe()));plan.pushKV("expected_output_bytes",static_cast<uint64_t>(ExpectedProbe().size()));plan.pushKV("cases",1);plan.pushKV("memory_bytes",256*1024*1024);plan.pushKV("wall_ms",2000);plan.pushKV("cpu_ms",1000);plan.pushKV("output_bytes",65536);
    const auto id=PlanId(plan);WritePrivate(f.root/"probe-input","probe",true);auto entry=Object();entry.pushKV("plan",plan);entry.pushKV("executable",f.probe.string());entry.pushKV("input_file",(f.root/"probe-input").string());auto plans=f.local["evaluation_plans"];plans.pushKV(id,entry);f.local.pushKV("evaluation_plans",plans);auto runtime=Object();runtime.pushKV("canary_package",id);auto runtimes=f.local["runtimes"];runtimes.pushKV(f.runtime,runtime);f.local.pushKV("runtimes",runtimes);f.SaveLocal();return id;
}
Json Control(Fixture& f){auto b=Common(f.realm,1,NowMs());b.pushKV("operation","resume");b.pushKV("nonce",Hex(RandomBytes(32)));return Signed("control",b,f.owner);}
}
int main(int argc,char** argv) {
    fs::path probe;fs::path report;
    if(argc>1)probe=fs::absolute(argv[1]);if(argc>2)report=fs::absolute(argv[2]);
    Harness h;
    h.Test("canonical-key-order",[]{Check(Canonical(Parse("{\"b\":2,\"a\":1}"))=="{\"a\":1,\"b\":2}","order");});
    h.Test("duplicate-json-keys-rejected",[]{Reject([]{Parse("{\"a\":1,\"a\":2}");},"SEC_JSON_DUPLICATE");});
    h.Test("float-json-rejected",[]{Reject([]{Parse("{\"a\":1.25}");});});
    h.Test("negative-zero-rejected",[]{Reject([]{Parse("{\"a\":-0}");});});
    h.Test("record-size-bounded",[]{Reject([]{Parse(std::string(MAX_RECORD+1,'x'));});});
    h.Test("mldsa-real-roundtrip-and-domain-separation",[]{auto k=GenerateKey();auto b=Object();b.pushKV("data",1);auto s=Unhex(Sign("receipt",b,k.secret_key));Check(Verify("receipt",b,k.public_key,s),"signature");Check(!Verify("grant",b,k.public_key,s),"domain");b.pushKV("data",2);Check(!Verify("receipt",b,k.public_key,s),"tamper");});
    h.Test("path-traversal-and-dos-names-rejected",[]{for(auto x:{"../outside","/root/a","a/../b","a\\b","CON","x/.ssh/id_rsa",".env.production"})Check(!IsPortablePath(x)||IsCredentialPath(x),"path accepted");});
    h.Test("policy-real-pq-pins-and-admission",[&]{Fixture f;auto a=AdmitCapsule(f.db,f.policy,f.capsule,f.now);Check(a.domains.size()==2&&a.hosts.size()==2,"coverage");Check(f.db.VerifyJournal(),"journal");});
    h.Test("one-domain-sybil-identities-insufficient",[&]{Fixture f({},true);Reject([&]{AdmitCapsule(f.db,f.policy,f.capsule,f.now);},"SEC_INDEPENDENT_COVERAGE");});
    h.Test("missing-gate-blocks-admission",[&]{Fixture f;auto refs=f.evaluations;refs.resize(10);auto id=f.Capsule(f.composition,refs);Reject([&]{AdmitCapsule(f.db,f.policy,id,f.now);},"SEC_INDEPENDENT_COVERAGE");});
    h.Test("unreferenced-negative-result-blocks-admission",[&]{Fixture f;auto ch=f.Challenge(f.composition,"adversarial");f.Import(f.Evaluation(ch,f.alice,"host-a","FAIL"));Reject([&]{AdmitCapsule(f.db,f.policy,f.capsule,f.now);},"SEC_NEGATIVE_EVIDENCE");});
    h.Test("dangling-execution-receipt-cannot-pass",[&]{Fixture f;auto e=f.db.Get(f.evaluations[0]);auto body=e["body"];body.pushKV("execution_receipt",Hash("absent receipt"));auto replacement=f.Import(Signed("evaluation",body,f.alice));auto refs=f.evaluations;refs[0]=replacement;auto cap=f.Capsule(f.composition,refs);Reject([&]{AdmitCapsule(f.db,f.policy,cap,f.now);},"SEC_OBJECT_ABSENT");});
    h.Test("modified-evaluation-signature-refused",[&]{Fixture f;auto e=f.db.Get(f.evaluations[0]);auto b=e["body"];b.pushKV("cases",999);e.pushKV("body",b);Reject([&]{f.Import(e);},"SEC_SIGNATURE");});
    h.Test("wrong-release-threshold-refused",[&]{Fixture f;auto e=f.db.Get(f.capsule);auto one=Envelope("capsule",e["body"]);AddSignature(one,f.owner);auto b=one["body"];b.pushKV("expires_ms",f.now+59000);one=Signed("capsule",b,f.owner);Reject([&]{f.Import(one);},"SEC_SIGNATURE_THRESHOLD");});
    h.Test("expiry-rechecked-at-admission",[&]{Fixture f;Reject([&]{AdmitCapsule(f.db,f.policy,f.capsule,f.now+60000);});});
    h.Test("publisher-revocation-quarantines-artifact",[&]{Fixture f;f.Revoke(KeyId(f.publisher.public_key));Reject([&]{AdmitCapsule(f.db,Policy::Load(f.db,NowMs()),f.capsule,NowMs());});});
    h.Test("revocation-inherited-through-lineage",[&]{Fixture f;auto child=f.MakeComposition("child",{f.composition});f.Revoke(f.composition);Check(f.db.AncestryRevoked(child,NowMs()),"lineage not quarantined");});
    h.Test("same-bytes-wrong-context-denied",[&]{Fixture f;auto g=f.Grant();Broker b(f.db,Hex(RandomBytes(32)));auto c=f.Context();c.pushKV("runtime",Hash("substitute"));Reject([&]{b.Prepare(g,c,f.ReadRequest(),NowMs());},"SEC_CONTEXT_BINDING");});
    h.Test("credential-resource-denied-before-permit",[&]{Fixture f;auto g=f.Grant();auto r=f.ReadRequest();r.pushKV("path",".env");Reject([&]{Broker(f.db,Hex(RandomBytes(32))).Prepare(g,f.Context(),r,NowMs());},"SEC_PROTECTED_RESOURCE");});
    h.Test("arbitrary-shell-and-network-tools-denied",[&]{for(auto tool:{"exec_command","shell","curl","network","download-and-execute"}){auto r=Object();r.pushKV("tool",tool);Reject([&]{RequestDigest(r);},"SEC_TOOL_DENIED");}});
    h.Test("read-effect-actually-dispatched",[&]{Fixture f;auto g=f.Grant();Broker b(f.db,Hex(RandomBytes(32)));auto p=b.Prepare(g,f.Context(),f.ReadRequest(),NowMs());auto r=b.Execute(Text(p,"secret"),f.Context(),f.ReadRequest(),NowMs());Check(Boolean(r,"ok")&&Boolean(r,"dispatched"),"effect not dispatched");auto bytes=Unhex(Text(r,"output_hex"));Check(std::string(bytes.begin(),bytes.end())=="hello safe workspace\n","read output");auto stored=b.Result(Text(p,"permit"));Check(!stored["result"].exists("output_hex"),"private output persisted");});
    h.Test("single-use-replay-refused",[&]{Fixture f;auto g=f.Grant();Broker b(f.db,Hex(RandomBytes(32)));auto p=b.Prepare(g,f.Context(),f.ReadRequest(),NowMs());b.Execute(Text(p,"secret"),f.Context(),f.ReadRequest(),NowMs());Reject([&]{b.Execute(Text(p,"secret"),f.Context(),f.ReadRequest(),NowMs());},"SEC_PERMIT_REPLAY");});
    h.Test("argument-substitution-refused",[&]{Fixture f;auto g=f.Grant();Broker b(f.db,Hex(RandomBytes(32)));auto p=b.Prepare(g,f.Context(),f.ReadRequest(),NowMs());auto changed=f.ReadRequest();changed.pushKV("path","link.txt");Reject([&]{b.Execute(Text(p,"secret"),f.Context(),changed,NowMs());},"SEC_ARGUMENT_SUBSTITUTION");});
    h.Test("restart-invalidates-bearer",[&]{Fixture f;auto g=f.Grant();auto p=Broker(f.db,Hex(RandomBytes(32))).Prepare(g,f.Context(),f.ReadRequest(),NowMs());Reject([&]{Broker(f.db,Hex(RandomBytes(32))).Execute(Text(p,"secret"),f.Context(),f.ReadRequest(),NowMs());},"SEC_PERMIT_EXPIRED_OR_RESTARTED");});
    h.Test("persistent-budget-cannot-reset-permit",[&]{Fixture f;auto g=f.Grant(1);Broker b(f.db,Hex(RandomBytes(32)));auto a=b.Prepare(g,f.Context(),f.ReadRequest(),NowMs());auto c=b.Prepare(g,f.Context(),f.ReadRequest(),NowMs());b.Execute(Text(a,"secret"),f.Context(),f.ReadRequest(),NowMs());Reject([&]{b.Execute(Text(c,"secret"),f.Context(),f.ReadRequest(),NowMs());},"SEC_BUDGET_EXHAUSTED");});
    h.Test("pause-resume-does-not-resurrect-permit",[&]{Fixture f;auto g=f.Grant();Broker b(f.db,Hex(RandomBytes(32)));auto p=b.Prepare(g,f.Context(),f.ReadRequest(),NowMs());b.Pause(NowMs());auto control=Control(f);b.Resume(control,NowMs());Reject([&]{b.Execute(Text(p,"secret"),f.Context(),f.ReadRequest(),NowMs());},"SEC_PERMIT_REPLAY");b.Pause(NowMs());Reject([&]{b.Resume(control,NowMs());},"SEC_RESUME_REPLAY");});
    h.Test("revocation-after-prepare-blocks-effect",[&]{Fixture f;auto g=f.Grant();Broker b(f.db,Hex(RandomBytes(32)));auto p=b.Prepare(g,f.Context(),f.ReadRequest(),NowMs());f.Revoke(g);Reject([&]{b.Execute(Text(p,"secret"),f.Context(),f.ReadRequest(),NowMs());});});
#ifndef _WIN32
    h.Test("symlink-read-escape-refused",[&]{Fixture f;fs::create_symlink(f.temp.root/"workspace"/".env",f.temp.root/"workspace"/"link.txt");auto g=f.Grant();Broker b(f.db,Hex(RandomBytes(32)));auto r=f.ReadRequest();r.pushKV("path","link.txt");auto p=b.Prepare(g,f.Context(),r,NowMs());auto result=b.Execute(Text(p,"secret"),f.Context(),r,NowMs());Check(!Boolean(result,"ok"),"symlink escaped");});
    h.Test("hardlink-read-escape-refused",[&]{Fixture f;fs::create_hard_link(f.temp.root/"workspace"/".env",f.temp.root/"workspace"/"link.txt");auto g=f.Grant();Broker b(f.db,Hex(RandomBytes(32)));auto r=f.ReadRequest();r.pushKV("path","link.txt");auto p=b.Prepare(g,f.Context(),r,NowMs());auto result=b.Execute(Text(p,"secret"),f.Context(),r,NowMs());Check(!Boolean(result,"ok"),"hardlink escaped");});
#endif
    h.Test("write-new-effect-and-overwrite-denial",[&]{Fixture f;auto g=f.Grant();Broker b(f.db,Hex(RandomBytes(32)));auto r=Object();r.pushKV("tool","write-new");r.pushKV("path","new.txt");r.pushKV("payload_hex","616263");auto p=b.Prepare(g,f.Context(),r,NowMs());Check(Boolean(b.Execute(Text(p,"secret"),f.Context(),r,NowMs()),"ok"),"write failed");Check(ReadFile(f.temp.root/"workspace"/"new.txt")=="abc","write bytes");p=b.Prepare(g,f.Context(),r,NowMs());Check(!Boolean(b.Execute(Text(p,"secret"),f.Context(),r,NowMs()),"ok"),"overwrite accepted");});
    h.Test("concurrent-replay-has-one-winner",[&]{Fixture f;auto g=f.Grant();const auto boot=Hex(RandomBytes(32));auto p=Broker(f.db,boot).Prepare(g,f.Context(),f.ReadRequest(),NowMs());std::atomic<int> successes{0};auto work=[&]{try{Database db(f.root);auto r=Broker(db,boot).Execute(Text(p,"secret"),f.Context(),f.ReadRequest(),NowMs());if(Boolean(r,"ok"))++successes;}catch(const Error&){};};std::thread one(work),two(work);one.join();two.join();Check(successes==1,"not exactly one dispatch");});
    h.Test("clock-rollback-refused",[&]{Fixture f;Database::Transaction tx(f.db);auto time=NowMs()+100;f.db.CheckClock(time);Reject([&]{f.db.CheckClock(time-1);},"SEC_CLOCK_ROLLBACK");});
    h.Test("resident-tamper-detected",[&]{Fixture f;auto path=f.root/"artifacts"/f.composition/"config.json";WritePrivate(path,"tampered",true);Reject([&]{VerifyResident(f.db,f.composition);});});
    h.Test("resident-extra-file-detected",[&]{Fixture f;WritePrivate(f.root/"artifacts"/f.composition/"unlisted.py","not executed");Reject([&]{VerifyResident(f.db,f.composition);},"SEC_RESIDENT_EXTRA_FILE");});
    h.Test("journal-tampering-detected",[&]{Fixture f;f.db.Exec("UPDATE events SET digest=? WHERE seq=1",{Hash("fake")});Check(!f.db.VerifyJournal(),"journal accepted corruption");});
    h.Test("stage-needs-owner-follow-and-resident-bytes",[&]{Fixture f;auto update=f.Import(f.Channel(1,f.capsule));Reject([&]{Lifecycle(f.db).Stage(update,NowMs());},"SEC_CHANNEL_NOT_FOLLOWED");auto m=f.Import(f.FollowMandate());Lifecycle(f.db).Follow(m,NowMs());Lifecycle(f.db).Stage(update,NowMs());Check(Text(Lifecycle(f.db).Status()[0],"phase")=="canary","not staged");Reject([&]{Lifecycle(f.db).Activate("coding/stable",NowMs());},"SEC_CANARY_DURATION");});
    h.Test("update-cannot-expand-runtime-authority",[&]{Fixture f;auto m=f.Import(f.FollowMandate(Hash("other runtime")));Lifecycle(f.db).Follow(m,NowMs());auto update=f.Import(f.Channel(1,f.capsule));Reject([&]{Lifecycle(f.db).Stage(update,NowMs());},"SEC_UPDATE_AUTHORITY_EXPANSION");});
    h.Test("signed-channel-equivocation-freezes-host",[&]{Fixture f;auto m=f.Import(f.FollowMandate());Lifecycle l(f.db);l.Follow(m,NowMs());auto a=f.Channel(1,f.capsule);l.Stage(f.Import(a),NowMs());auto b=a["body"];b.pushKV("expires_ms",f.now+59000);auto other=f.Import(Signed("channel",b,f.owner,&f.second));Reject([&]{l.Stage(other,NowMs());},"SEC_CHANNEL_EQUIVOCATION");Check(Text(l.Status()[0],"phase")=="frozen","not frozen");});
    h.Test("peer-cannot-import-local-grant",[&]{Fixture f;auto grant=f.db.Get(f.Grant());Reject([&]{ImportRecord(f.db,f.policy,grant,NowMs(),true);},"SEC_PEER_CANNOT_GRANT");});
    h.Test("public-evidence-closure-excludes-grants",[&]{Fixture f;PublishEvidenceClosure(f.db,f.capsule,NowMs());Check(!f.db.Query("SELECT id FROM objects WHERE id=? AND visibility='public'",{f.capsule}).empty(),"capsule unpublished");auto grant=f.Grant();Reject([&]{PublishEvidenceClosure(f.db,grant,NowMs());},"SEC_PRIVATE_KIND");});
    h.Test("peer-authenticated-inventory-and-replay-rejection",[&]{Fixture f;PublishEvidenceClosure(f.db,f.capsule,NowMs());auto b=Common(f.realm,1,NowMs(),30000);b.pushKV("sender",KeyId(f.bob.public_key));b.pushKV("recipient",KeyId(f.alice.public_key));b.pushKV("phase","request");b.pushKV("nonce",Hex(RandomBytes(32)));b.pushKV("cursor","");b.pushKV("want",Strings({f.capsule}));auto request=Signed("peer",b,f.bob);Reject([&]{HandlePeerRequest(f.db,request,Hash("wrong-pin"),NowMs());},"SEC_PEER_TRANSPORT_PIN");auto response=HandlePeerRequest(f.db,request,Hash("pin-b"),NowMs());Check(response["body"]["records"].size()==1,"record missing");Reject([&]{HandlePeerRequest(f.db,request,Hash("pin-b"),NowMs());},"SEC_PEER_REPLAY");});
    h.Test("private-object-and-missing-object-indistinguishable",[&]{Fixture f;auto grant=f.Grant();auto b=Common(f.realm,1,NowMs(),30000);b.pushKV("sender",KeyId(f.bob.public_key));b.pushKV("recipient",KeyId(f.alice.public_key));b.pushKV("phase","request");b.pushKV("nonce",Hex(RandomBytes(32)));b.pushKV("cursor","");b.pushKV("want",Strings({grant,Hash("absent")}));auto response=HandlePeerRequest(f.db,Signed("peer",b,f.bob),Hash("pin-b"),NowMs());Check(response["body"]["records"].size()==0&&response["body"]["not_available"].size()==2,"private oracle");});
#if defined(__linux__)
    h.Test("native-sandbox-denies-file-network-fork-and-env",[&]{Check(!probe.empty(),"probe path required");setenv("BTX_SECRET_SENTINEL","SYNTHETIC_NOT_A_CREDENTIAL",1);auto bytes=ReadFile(probe,128*1024*1024);auto r=RunStaticWorker(probe,Hash(bytes),"probe",SandboxLimits{},[]{return true;});unsetenv("BTX_SECRET_SENTINEL");Check(r.dispatched&&r.enforced&&r.error.empty()&&r.exit_code==0&&r.output==ExpectedProbe(),r.error.c_str());});
    h.Test("native-sandbox-executable-substitution-refused",[&]{Reject([&]{RunStaticWorker(probe,Hash("wrong"),"",SandboxLimits{},[]{return true;});},"SEC_EXECUTABLE_DIGEST");});
    h.Test("native-sandbox-timeout-kills-owned-child",[&]{auto limits=SandboxLimits{};limits.wall_ms=100;limits.cpu_ms=100;auto r=RunStaticWorker(probe,Hash(ReadFile(probe,128*1024*1024)),"sleep",limits,[]{return true;});Check(r.timed_out&&!r.error.empty()&&r.output.empty(),"timeout missing");});
    h.Test("native-sandbox-output-cap",[&]{auto limits=SandboxLimits{};limits.output_bytes=128;auto r=RunStaticWorker(probe,Hash(ReadFile(probe,128*1024*1024)),"flood",limits,[]{return true;});Check(r.error=="SEC_OUTPUT_LIMIT"&&r.output.empty(),"output escaped");});
    h.Test("native-sandbox-revocation-cancels-and-withholds",[&]{std::atomic<int> count{0};auto r=RunStaticWorker(probe,Hash(ReadFile(probe,128*1024*1024)),"sleep",SandboxLimits{},[&]{return ++count<4;});Check(r.cancelled&&!r.error.empty()&&r.output.empty(),"cancel missing");});
    h.Test("signed-action-actually-executes-confined-worker",[&]{Fixture f(probe);auto package=f.InstallProbePackage();auto g=f.Grant();auto request=Object();request.pushKV("tool","run-static");request.pushKV("package",package);request.pushKV("payload_hex","70726f6265");Broker b(f.db,Hex(RandomBytes(32)));auto p=b.Prepare(g,f.Context(),request,NowMs());auto r=b.Execute(Text(p,"secret"),f.Context(),request,NowMs());Check(Boolean(r,"ok")&&Boolean(r,"sandbox_enforced")&&Boolean(r,"dispatched"),r.write().c_str());});
    h.Test("real-evaluation-produces-bound-signed-receipt",[&]{Fixture f(probe);auto package=InstallPlan(f);auto challenge=f.Challenge(f.composition,"adversarial",package);auto id=f.Import(challenge);auto report=EvaluateChallenge(f.db,id,NowMs());Check(Text(report["body"],"outcome")=="PASS","probe evaluation failed");auto receipt=f.db.Get(Text(report["body"],"execution_receipt"));Check(Boolean(receipt["body"],"dispatched"),"not dispatched");Check(Text(receipt["body"],"request")==id,"request binding");Reject([&]{EvaluateChallenge(f.db,id,NowMs());},"SEC_EVALUATION_ALREADY_ATTEMPTED");});
    h.Test("real-canaries-activate-persistent-model-binding",[&]{Fixture f(probe);InstallPlan(f);Lifecycle l(f.db);auto mandate=f.Import(f.FollowMandate());l.Follow(mandate,NowMs());auto update=f.Import(f.Channel(1,f.capsule));l.Stage(update,NowMs());auto first=RunChannelCanary(f.db,"coding/stable",NowMs());l.Observe("coding/stable",RecordId(first),NowMs());std::this_thread::sleep_for(std::chrono::milliseconds(1050));auto second=RunChannelCanary(f.db,"coding/stable",NowMs());l.Observe("coding/stable",RecordId(second),NowMs());l.Activate("coding/stable",NowMs());Check(Text(l.Status()[0],"phase")=="active","not activated");Check(Text(l.Status()[0],"active_capsule")==f.capsule,"wrong active");f.Revoke(f.composition);l.Tick(NowMs());Check(Text(l.Status()[0],"phase")=="frozen","incident did not freeze");});
#endif
    h.Test("private-protocol-framing-and-out-of-order-denial",[&]{Temp t;Service service(t.root/"security");GuardProtocol protocol(service);const auto id=Hex(RandomBytes(16));const auto request=std::string("{\"op\":\"status\"}");Check(protocol.Handle("SEC2 BEGIN "+id+" "+std::to_string(request.size()))=="SEC2 READY "+id,"begin");auto hex=Hex(std::span<const unsigned char>(reinterpret_cast<const unsigned char*>(request.data()),request.size()));Check(protocol.Handle("SEC2 DATA "+id+" 1 "+hex).starts_with("ERR "),"out-of-order accepted");protocol.Handle("SEC2 BEGIN "+id+" "+std::to_string(request.size()));Check(protocol.Handle("SEC2 DATA "+id+" 0 "+hex).starts_with("SEC2 STORED "),"data");Check(protocol.Handle("SEC2 COMMIT "+id).starts_with("SEC2 RESPONSE "),"commit");Check(protocol.Handle("SEC2 READ "+id+" 0").starts_with("SEC2 BYTES "),"response");});
    h.Test("async-service-exec-and-delayed-release-fence",[&]{Fixture f;auto grant=f.Grant();Service service(f.root);auto request=Object();request.pushKV("op","prepare");request.pushKV("grant",grant);request.pushKV("context",f.Context());request.pushKV("request",f.ReadRequest());auto p=service.Handle(request);Check(Boolean(p,"ok"),p.write().c_str());request=Object();request.pushKV("op","execute");request.pushKV("secret",p["secret"]);request.pushKV("context",f.Context());request.pushKV("request",f.ReadRequest());auto submitted=service.Handle(request);Check(Boolean(submitted,"ok"),"submit");auto poll=Object();poll.pushKV("op","poll");poll.pushKV("job",submitted["job"]);Json result;for(int i=0;i<100;++i){result=service.Handle(poll);if(result["phase"].isStr()&&result["phase"].get_str()=="finished")break;std::this_thread::sleep_for(std::chrono::milliseconds(10));}Check(Text(result,"phase")=="finished","job did not finish");f.Revoke(grant);poll.pushKV("op","take");result=service.Handle(poll);Check(!Boolean(result["result"],"ok")&&!result["result"].exists("output_hex"),"revoked queued output released");});
    Json summary=Object();summary.pushKV("schema","BTX/ComponentSecurityTestResults/v1");summary.pushKV("passed",static_cast<uint64_t>(h.passed));summary.pushKV("failed",static_cast<uint64_t>(h.failed));summary.pushKV("physical_hosts",1);summary.pushKV("independent_organizations_verified",false);summary.pushKV("gpu_execution","NOT_RUN");summary.pushKV("whole_btxd_build","NOT_RUN");summary.pushKV("native_windows","NOT_RUN");summary.pushKV("native_macos","NOT_RUN");summary.pushKV("behavioral_backdoor_absence_proven",false);summary.pushKV("tests",h.results);if(!report.empty()){EnsurePrivateDirectory(report.parent_path());WritePrivate(report,Canonical(summary)+"\n",true);}std::cout<<"SUMMARY "<<Canonical(summary)<<'\n';return h.failed?1:0;
}
