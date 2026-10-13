#include <cstdlib>

bool FailStr(std::string& err, const std::string& code)
{
    err = code.empty() ? std::string("BTX035_SCHEMA") : code;
    return false;
}

bool HexN(const std::string& hex, size_t n)
{
    if (hex.size() != n) return false;
    for (unsigned char c : hex) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

bool ParseDec(const std::string& s, int64_t& out)
{
    if (s.empty() || s.size() > 18) return false;
    if (s.size() > 1 && s[0] == '0') return false;
    int64_t n = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        n = n * 10 + (c - '0');
    }
    out = n;
    return true;
}

bool Expect(Store& store, const UniValue& req, const std::string& subject, std::string& err)
{
    store.token_subject = subject;
    if (!req.exists("expected_state") || req["expected_state"].isNull()) return true;
    if (!req["expected_state"].isStr() || !HexN(req["expected_state"].get_str(), 96)) return Fail(err, "BTX035_SCHEMA");
    const auto it = store.tokens.find(subject);
    if (it == store.tokens.end() || it->second != req["expected_state"].get_str()) return Fail(err, "BTX035_STATE");
    return true;
}

bool Rec(UniValue& result, const std::string& id, const char* state)
{
    result.pushKV("record_id", id);
    result.pushKV("state", state);
    return true;
}

bool OpOut(Store& store, UniValue& result, const std::string& id, const char* state, const char* physical)
{
    Operation op;
    op.state = state;
    op.physical = physical;
    store.operations[id] = op;
    result.pushKV("operation_id", id);
    result.pushKV("state", state);
    return true;
}

UniValue RefOf(const char* kind, const std::string& digest)
{
    UniValue ref(UniValue::VOBJ);
    ref.pushKV("kind", kind);
    ref.pushKV("digest48", digest);
    return ref;
}

bool LoadEnvelope(Store& store, const std::string& id, UniValue& envelope, std::string& err)
{
    const auto it = store.records.find(id);
    if (it == store.records.end()) return Fail(err, "BTX035_NOT_FOUND");
    if (!envelope.read(it->second) || !envelope.isObject()) return Fail(err, "BTX035_ENVELOPE");
    std::string verify_err;
    if (!VerifySignedEnvelopeV11(envelope, verify_err)) return FailStr(err, verify_err);
    return true;
}

fs::path BundleDir()
{
    if (const char* env = std::getenv("BTX035_BUNDLE_DIR")) return fs::path(env);
    return fs::path("lab-bundles");
}

bool SafeSegment(const std::string& name)
{
    if (name.empty() || name.size() > 80 || name == "." || name == "..") return false;
    for (unsigned char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        if (!ok) return false;
    }
    return true;
}

bool EnsureBundleDir(std::string& err)
{
    if (std::filesystem::is_directory(BundleDir())) return true;
    if (!fs::create_directories(BundleDir())) return Fail(err, "BTX035_BUNDLE");
    return true;
}

bool SignRequest(Store& store, const UniValue& req, UniValue& envelope, std::string& err)
{
    if (store.active_root.empty() || !store.identity_sk.count(store.active_root)) return Fail(err, "BTX035_NOT_FOUND");
    const std::vector<unsigned char>& pk = store.identity_pk.at(store.active_root);
    const std::vector<unsigned char>& sk = store.identity_sk.at(store.active_root);
    const std::string signer = ResearchIdentityId(Span<const unsigned char>{pk.data(), pk.size()}).Hex();
    UniValue body(UniValue::VOBJ);
    body.pushKV("envelope_version", 2);
    body.pushKV("contract_revision", "1.1.0");
    body.pushKV("record_type", req["record_type"].get_str());
    body.pushKV("network_id", Id64("BTX/035/network"));
    body.pushKV("realm_id", req["realm_id"].get_str());
    body.pushKV("principal_id", signer);
    body.pushKV("signer_id", signer);
    body.pushKV("key_epoch", "0");
    body.pushKV("public_key_hex", HexStr(pk));
    body.pushKV("delegation_id", req["delegation_id"].isNull() ? UniValue() : req["delegation_id"]);
    body.pushKV("created_ms", "1");
    body.pushKV("expires_ms", req["expires_ms"].get_str());
    body.pushKV("critical_extensions", UniValue(UniValue::VARR));
    body.pushKV("payload", req["payload"]);
    Digest48 id;
    if (!RecordIdV11(body, id, err)) return false;
    std::vector<unsigned char> pre;
    if (!RecordPreimageV11(body, pre, err)) return false;
    std::vector<unsigned char> sig;
    if (!SignMlDsa44(Span<const unsigned char>{sk.data(), sk.size()},
                    Span<const unsigned char>{pre.data(), pre.size()}, sig, err)) {
        return false;
    }
    envelope.pushKV("record_id", id.Hex());
    envelope.pushKV("body", body);
    envelope.pushKV("signature_hex", HexStr(sig));
    std::string verify_err;
    if (!VerifySignedEnvelopeV11(envelope, verify_err)) return FailStr(err, verify_err);
    return true;
}

void QueryOut(UniValue& result, const std::vector<std::string>& ids, const std::string& salt)
{
    UniValue arr(UniValue::VARR);
    for (const std::string& id : ids) {
        if (HexN(id, 96)) arr.push_back(id);
    }
    result.pushKV("record_ids", arr);
    result.pushKV("next_cursor", UniValue());
    UniValue coverage = Coverage("LOCAL");
    coverage.pushKV("view_id", Id96("coverage|" + salt));
    coverage.pushKV("observed_records", std::to_string(arr.size()));
    result.pushKV("coverage", coverage);
    result.pushKV("snapshot_id", Id96("snapshot|" + salt));
    result.pushKV("policy_id", UniValue());
    result.pushKV("truncated_by_bytes", false);
}

void FillOp(UniValue& result, const Operation& op)
{
    result.pushKV("state", op.state);
    result.pushKV("progress", UniValue(UniValue::VARR));
    result.pushKV("result_ref", UniValue());
    result.pushKV("error", UniValue());
    result.pushKV("physical_disposition", op.physical);
    result.pushKV("generation", std::to_string(op.generation));
    result.pushKV("lease_count", "0");
    result.pushKV("quiescence_evidence_id", UniValue());
    result.pushKV("completion_result", UniValue());
}

bool Apply(const std::string& method, const UniValue& req, UniValue& result, std::string& err)
{
    Store& store = State();
    store.token_subject.clear();
    const std::string id = Id96(method + "|" + req.write());

    if (method == "lab.info") {
        result.pushKV("release", "0.35.0-private");
        result.pushKV("network_id", Id64("BTX/035/network"));
        result.pushKV("realm_id", Id96("BTX/035/PublicRealm/v1"));
        UniValue protocols(UniValue::VARR);
        protocols.push_back("3");
        result.pushKV("protocols", protocols);
        UniValue capabilities(UniValue::VARR);
        capabilities.push_back("lab");
        capabilities.push_back("git");
        capabilities.push_back("research");
        result.pushKV("capabilities", capabilities);
        result.pushKV("baseline_commit", "0f36daafd42b4b921696df9d4f3de4baeff437c8");
        return true;
    }
    if (method == "lab.record_get") {
        UniValue envelope;
        if (!LoadEnvelope(store, RequireStr(req, "record_id"), envelope, err)) return false;
        result.pushKV("envelope", envelope);
        result.pushKV("signature_valid", true);
        result.pushKV("authority_state", "VALID");
        result.pushKV("source", "LOCAL");
        return true;
    }
    if (method == "lab.record_verify") {
        std::string verify_err;
        const bool valid = VerifySignedEnvelopeV11(req["envelope"], verify_err);
        result.pushKV("valid", valid);
        UniValue reasons(UniValue::VARR);
        if (!valid && !verify_err.empty()) reasons.push_back(verify_err);
        result.pushKV("reason_codes", reasons);
        return true;
    }
    if (method == "lab.record_import") {
        if (!Expect(store, req, "import|" + RequireStr(req, "idempotency_key"), err)) return false;
        std::string verify_err;
        if (!VerifySignedEnvelopeV11(req["envelope"], verify_err)) return FailStr(err, verify_err);
        const std::string record_id = req["envelope"]["record_id"].get_str();
        store.records[record_id] = req["envelope"].write();
        return Rec(result, record_id, req["publish"].isTrue() ? "PUBLISHED" : "STORED");
    }
    if (method == "lab.identity_create") {
        if (!Expect(store, req, "identity|" + RequireStr(req, "label"), err)) return false;
        std::vector<unsigned char> pk, sk;
        if (!GenerateMlDsa44(pk, sk, err)) return false;
        Digest48 root;
        if (!store.chain.CreateRoot(Span<const unsigned char>{pk.data(), pk.size()}, root, err)) {
            memory_cleanse(sk.data(), sk.size());
            return false;
        }
        const std::string root_id = root.Hex();
        store.identity_pk[root_id] = std::move(pk);
        store.identity_sk[root_id] = std::move(sk);
        store.active_root = root_id;
        store.identities[root_id] = "ACTIVE";
        return Rec(result, root_id, "ACTIVE");
    }
    if (method == "lab.identity_inspect") {
        const std::string principal = RequireStr(req, "principal_id");
        if (!HexN(principal, 96)) return Fail(err, "BTX035_SCHEMA");
        result.pushKV("principal_id", principal);
        Digest48 root;
        uint64_t epoch = 0;
        std::string ignored;
        if (!Digest48::FromHex(principal, root, ignored) || !store.chain.CurrentEpoch(root, epoch, ignored)) {
            result.pushKV("active_key_id", UniValue());
            result.pushKV("epoch", UniValue());
            result.pushKV("state", "UNKNOWN");
            result.pushKV("chain_ids", UniValue(UniValue::VARR));
            return true;
        }
        std::vector<unsigned char> pk;
        std::string key_err;
        UniValue chains(UniValue::VARR);
        chains.push_back(principal);
        result.pushKV("chain_ids", chains);
        if (store.chain.CurrentPublicKey(root, pk, key_err)) {
            result.pushKV("active_key_id", IdentityKeyId(Span<const unsigned char>{pk.data(), pk.size()}).Hex());
        } else {
            result.pushKV("active_key_id", UniValue());
        }
        result.pushKV("epoch", std::to_string(epoch));
        const auto it = store.identities.find(principal);
        result.pushKV("state", it == store.identities.end() ? "ACTIVE" : it->second);
        return true;
    }
    if (method == "lab.identity_sign") {
        if (!Expect(store, req, "sign|" + store.active_root, err)) return false;
        UniValue envelope(UniValue::VOBJ);
        if (!SignRequest(store, req, envelope, err)) return false;
        result.pushKV("envelope", envelope);
        return true;
    }
    if (method == "lab.identity_transition" || method == "lab.identity_revoke") {
        if (!Expect(store, req, method, err)) return false;
        const std::string proof_id = method == "lab.identity_revoke" ? RequireStr(req, "revocation_id") : RequireStr(req, "new_key_proof_id");
        UniValue envelope;
        if (!LoadEnvelope(store, proof_id, envelope, err)) return false;
        const UniValue& body = envelope["body"];
        if (method == "lab.identity_revoke") {
            if (body["record_type"].get_str() != "Revocation") return Fail(err, "BTX035_RECORD_TYPE");
            Digest48 root;
            Digest48 key;
            if (!Digest48::FromHex(body["principal_id"].get_str(), root, err)) return false;
            if (!Digest48::FromHex(body["signer_id"].get_str(), key, err)) return false;
            if (!store.chain.RevokeKey(root, key, 1, err)) return false;
            store.identities[root.Hex()] = "REVOKED";
            return Rec(result, proof_id, "REVOKED");
        }
        if (body["record_type"].get_str() != "NewKeyProof") return Fail(err, "BTX035_RECORD_TYPE");
        const UniValue& payload = body["payload"];
        if (!payload.isObject() || !payload.exists("root_id") || !payload["root_id"].isStr() ||
            !payload.exists("new_epoch") || !payload["new_epoch"].isStr() ||
            !payload.exists("new_public_key_hex") || !payload["new_public_key_hex"].isStr()) {
            return Fail(err, "BTX035_SCHEMA");
        }
        Digest48 root;
        int64_t new_epoch = 0;
        if (!Digest48::FromHex(payload["root_id"].get_str(), root, err)) return false;
        if (!ParseDec(payload["new_epoch"].get_str(), new_epoch)) return Fail(err, "BTX035_SCHEMA");
        const std::vector<unsigned char> npk = ParseHex(payload["new_public_key_hex"].get_str());
        const std::vector<unsigned char> sig = ParseHex(envelope["signature_hex"].get_str());
        if (!store.chain.AdvanceEpoch(root, npk, static_cast<uint64_t>(new_epoch), sig, err)) return false;
        store.identities[root.Hex()] = "ACTIVE";
        return Rec(result, proof_id, "ACTIVE");
    }
    if (method == "lab.identity_refresh") {
        if (!Expect(store, req, "refresh|" + RequireStr(req, "principal_id"), err)) return false;
        Digest48 root;
        uint64_t epoch = 0;
        if (!Digest48::FromHex(RequireStr(req, "principal_id"), root, err) || !store.chain.CurrentEpoch(root, epoch, err)) {
            return Fail(err, "BTX035_NOT_FOUND");
        }
        result.pushKV("identity_state_id", Id96(root.Hex() + "|" + std::to_string(epoch)));
        return true;
    }
    if (method == "lab.grant_evaluate") {
        const std::string grant = RequireStr(req, "grant_id");
        const bool allow = store.grants.count(grant) != 0;
        result.pushKV("decision", allow ? "ALLOW" : "DENY");
        result.pushKV("effective_limits", UniValue(UniValue::VARR));
        UniValue reasons(UniValue::VARR);
        if (!allow) reasons.push_back("BTX035_PERMIT");
        result.pushKV("reason_codes", reasons);
        return true;
    }
    if (method == "lab.research_subscribe") {
        if (!Expect(store, req, "sub|" + RequireStr(req, "idempotency_key"), err)) return false;
        const std::string sub = Id96("sub|" + req.write());
        store.subscription_cursor[sub] = store.published.size();
        result.pushKV("subscription_id", sub);
        return true;
    }
    if (method == "lab.research_unsubscribe") {
        if (!Expect(store, req, "unsub|" + RequireStr(req, "subscription_id"), err)) return false;
        const std::string sub = RequireStr(req, "subscription_id");
        if (!store.subscription_cursor.count(sub)) return Fail(err, "BTX035_NOT_FOUND");
        store.subscription_cursor.erase(sub);
        result.pushKV("stopped", true);
        return true;
    }
    if (method == "lab.research_events") {
        const std::string sub = RequireStr(req, "subscription_id");
        if (!store.subscription_cursor.count(sub)) return Fail(err, "BTX035_NOT_FOUND");
        int64_t after = 0;
        if (!ParseDec(RequireStr(req, "after_sequence"), after)) return Fail(err, "BTX035_SCHEMA");
        UniValue events(UniValue::VARR);
        bool gap = false;
        if (after > static_cast<int64_t>(store.published.size())) gap = true;
        else {
            for (size_t i = static_cast<size_t>(after); i < store.published.size() && events.size() < 100; ++i) {
                events.push_back(store.published[i]);
            }
        }
        result.pushKV("events", events);
        result.pushKV("next_sequence", std::to_string(store.published.size()));
        result.pushKV("gap", gap);
        return true;
    }
    if (method == "lab.research_publish") {
        if (!Expect(store, req, "publish|" + RequireStr(req, "record_id"), err)) return false;
        if (store.active_root.empty()) return Fail(err, "BTX035_NOT_FOUND");
        if (!req["topic_ids"].isArray() || req["topic_ids"].empty()) return Fail(err, "BTX035_SCHEMA");
        const std::string record_id = RequireStr(req, "record_id");
        const std::string topic_hex = req["topic_ids"][0].isStr() ? req["topic_ids"][0].get_str() : "";
        if (!HexN(record_id, 96) || !HexN(topic_hex, 96)) return Fail(err, "BTX035_SCHEMA");
        Digest48 topic, cid, author;
        if (!Digest48::FromHex(topic_hex, topic, err) || !Digest48::FromHex(record_id, cid, err) ||
            !Digest48::FromHex(store.active_root, author, err)) {
            return false;
        }
        if (!store.research.Publish(topic, cid, author, false, record_id, err)) return false;
        store.published.push_back(record_id);
        store.records[record_id] = req.write();
        return Rec(result, record_id, "PUBLISHED");
    }
    if (method == "lab.research_query" || method == "lab.research_frontier" || method == "lab.git_refs" || method == "lab.agent_lineage") {
        std::vector<std::string> ids = store.published;
        if (method == "lab.agent_lineage") {
            ids.clear();
            const std::string release = RequireStr(req, "release_id");
            if (store.lineage.count(release)) ids.push_back(store.lineage[release]);
        }
        if (method == "lab.git_refs") {
            ids.clear();
            const auto it = store.repo_commit.find(RequireStr(req, "repository_id"));
            if (it != store.repo_commit.end() && HexN(it->second, 96)) ids.push_back(it->second);
        }
        QueryOut(result, ids, method + req.write());
        return true;
    }
    if (method == "lab.research_context" || method == "lab.source_query") {
        result.pushKV("result_ref", RefOf("RECORD", Id96(method + req.write())));
        result.pushKV("coverage", Coverage("LOCAL"));
        if (method == "lab.source_query") result.pushKV("index_source_roots", UniValue(UniValue::VARR));
        return true;
    }
    if (method == "lab.research_execute" || method == "lab.artifact_acquire" || method == "lab.agent_adopt" ||
        method == "lab.promotion_verify" || method == "lab.workspace_hydrate" || method == "lab.workspace_restore") {
        if (!Expect(store, req, method + "|" + RequireStr(req, "idempotency_key"), err)) return false;
        const char* physical = "NOT_DISPATCHED";
        const char* state = "RUNNING";
        if (method == "lab.research_execute" && !store.grants.count(RequireStr(req, "local_capability_grant_id"))) {
            state = "FAILED";
            physical = "STOPPED_QUIESCENT";
        }
        if (method == "lab.agent_adopt") {
            bool ready = store.grants.count(RequireStr(req, "grant_id")) != 0 && req["evaluation_ids"].isArray() && !req["evaluation_ids"].empty();
            if (ready) {
                for (size_t i = 0; i < req["evaluation_ids"].size(); ++i) {
                    if (!req["evaluation_ids"][i].isStr() || !store.records.count(req["evaluation_ids"][i].get_str())) ready = false;
                }
            }
            if (!ready) {
                state = "FAILED";
                physical = "STOPPED_QUIESCENT";
            } else {
                store.lineage[RequireStr(req, "release_id")] = RequireStr(req, "prior_release_id");
                state = "COMPLETE";
                physical = "STOPPED_QUIESCENT";
            }
        }
        if (method == "lab.promotion_verify" && !store.records.count(RequireStr(req, "promotion_id"))) {
            state = "FAILED";
            physical = "STOPPED_QUIESCENT";
        }
        if (method == "lab.workspace_hydrate" && !store.workspace_backend.count(RequireStr(req, "workspace_id"))) return Fail(err, "BTX035_NOT_FOUND");
        if (method == "lab.workspace_restore") {
            if (!store.checkpoints.count(RequireStr(req, "checkpoint_id"))) return Fail(err, "BTX035_NOT_FOUND");
            const std::string ws = store.checkpoints[RequireStr(req, "checkpoint_id")];
            if (!store.workspaces.Undo(ws, err)) return false;
            state = "COMPLETE";
            physical = "STOPPED_QUIESCENT";
        }
        return OpOut(store, result, id, state, physical);
    }
    if (method == "lab.git_register" || method == "lab.git_snapshot") {
        if (!Expect(store, req, method + "|" + RequireStr(req, "repository_id"), err)) return false;
        const std::string repo = RequireStr(req, "repository_id");
        if (!store.repos.count(repo)) return Fail(err, "BTX035_NOT_FOUND");
        if (method == "lab.git_register") {
            store.repo_commit[repo] = RequireStr(req, "required_commit");
            return OpOut(store, result, id, "COMPLETE", "STOPPED_QUIESCENT");
        }
        const std::string commit = RequireStr(req, "commit");
        const std::vector<unsigned char> bytes(commit.begin(), commit.end());
        std::string digest;
        if (!GitObjectDigest("commit", Span<const unsigned char>{bytes.data(), bytes.size()}, digest, err)) return false;
        store.records[Id96(digest)] = digest;
        if (req["history_required"].isTrue()) return OpOut(store, result, id, "FAILED", "STOPPED_QUIESCENT");
        store.snapshots.insert(id);
        return OpOut(store, result, id, "COMPLETE", "STOPPED_QUIESCENT");
    }
    if (method == "lab.git_fetch_ticket") {
        if (!Expect(store, req, "ticket|" + RequireStr(req, "repository_id"), err)) return false;
        if (!store.repos.count(RequireStr(req, "repository_id"))) return Fail(err, "BTX035_NOT_FOUND");
        const std::string ticket = Id64("ticket|" + RequireStr(req, "repository_id") + "|" + RequireStr(req, "idempotency_key"));
        result.pushKV("ticket", ticket);
        result.pushKV("expires_ms", "30000");
        result.pushKV("socket_path", "btx-git:" + ticket);
        return true;
    }
    if (method == "lab.git_attest") {
        if (!Expect(store, req, "attest|" + RequireStr(req, "snapshot_id"), err)) return false;
        if (!store.snapshots.count(RequireStr(req, "snapshot_id"))) return Fail(err, "BTX035_NOT_FOUND");
        return Rec(result, Id96("attest|" + RequireStr(req, "snapshot_id")), "ATTESTED");
    }
    if (method == "lab.git_publish_ref") {
        if (!Expect(store, req, "ref|" + RequireStr(req, "update_id"), err)) return false;
        UniValue envelope;
        if (!LoadEnvelope(store, RequireStr(req, "update_id"), envelope, err)) return false;
        if (envelope["body"]["record_type"].get_str() != "GitRefUpdate") return Fail(err, "BTX035_RECORD_TYPE");
        return Rec(result, RequireStr(req, "update_id"), "PUBLISHED");
    }
    if (method == "lab.git_push_authorize") {
        if (!Expect(store, req, "push|" + RequireStr(req, "batch_id"), err)) return false;
        if (!HexN(RequireStr(req, "batch_id"), 96)) return Fail(err, "BTX035_SCHEMA");
        result.pushKV("transaction_id", Id96("tx|" + RequireStr(req, "batch_id")));
        result.pushKV("state", "PREPARED");
        return true;
    }
    if (method == "lab.repository_create") {
        if (!Expect(store, req, "repo|" + RequireStr(req, "label"), err)) return false;
        const std::string repo = Id96("repo|" + RequireStr(req, "label") + "|" + RequireStr(req, "nonce"));
        const std::string genesis = HexN(RequireStr(req, "project_genesis_id"), 96) ? RequireStr(req, "project_genesis_id") : Id96("genesis|" + repo);
        store.repos.insert(repo);
        store.repo_commit[repo] = genesis;
        result.pushKV("repository_id", repo);
        result.pushKV("repository_genesis_id", genesis);
        return true;
    }
    if (method == "lab.workspace_create") {
        if (!Expect(store, req, "ws|" + RequireStr(req, "snapshot_id"), err)) return false;
        const std::string backend = RequireStr(req, "backend");
        if (backend == "EXTERNAL_ADAPTER") return Fail(err, "BTX035_WORKSPACE");
        if (backend != "PLAIN_GIT" && backend != "OPERATION_JOURNAL") return Fail(err, "BTX035_SCHEMA");
        const std::string ws = Id96("ws|" + RequireStr(req, "snapshot_id") + "|" + backend);
        if (!store.workspaces.OpenTree(ws, err)) return false;
        store.workspace_backend[ws] = backend;
        result.pushKV("workspace_id", ws);
        result.pushKV("generation", std::to_string(store.workspaces.Generation(ws)));
        result.pushKV("path", "ws/" + ws);
        return true;
    }
    if (method == "lab.workspace_operation") {
        if (!Expect(store, req, "wsop|" + RequireStr(req, "workspace_id"), err)) return false;
        const std::string ws = RequireStr(req, "workspace_id");
        if (!store.workspace_backend.count(ws)) return Fail(err, "BTX035_NOT_FOUND");
        const uint64_t gen = store.workspaces.Generation(ws);
        const std::string raw = req["event"].write();
        const std::vector<unsigned char> bytes(raw.begin(), raw.end());
        if (!store.workspaces.Put(ws, gen, "event", bytes, err)) return false;
        if (!store.workspaces.Publish(ws, gen, false, false, err)) return false;
        result.pushKV("sequence", std::to_string(gen));
        result.pushKV("journal_state", Id96(ws + "|" + std::to_string(gen)));
        return true;
    }
    if (method == "lab.workspace_snapshot") {
        if (!Expect(store, req, "wssnap|" + RequireStr(req, "workspace_id"), err)) return false;
        const std::string ws = RequireStr(req, "workspace_id");
        if (!store.workspace_backend.count(ws)) return Fail(err, "BTX035_NOT_FOUND");
        int64_t want = 0;
        if (!ParseDec(RequireStr(req, "generation"), want)) return Fail(err, "BTX035_SCHEMA");
        const uint64_t gen = store.workspaces.Generation(ws);
        if (static_cast<uint64_t>(want) != gen) return Fail(err, "BTX035_GENERATION");
        if (!store.workspaces.Publish(ws, gen, false, req["publishable"].isTrue(), err)) return false;
        const std::string cp = Id96(ws + "|snap|" + std::to_string(gen));
        store.checkpoints[cp] = ws;
        return Rec(result, cp, "SNAPSHOT");
    }
    if (method == "lab.workspace_reconcile") {
        if (!Expect(store, req, "wsrec|" + RequireStr(req, "workspace_id"), err)) return false;
        if (!store.workspace_backend.count(RequireStr(req, "workspace_id"))) return Fail(err, "BTX035_NOT_FOUND");
        bool ready = req["parent_checkpoint_ids"].isArray() && !req["parent_checkpoint_ids"].empty();
        if (ready) {
            for (size_t i = 0; i < req["parent_checkpoint_ids"].size(); ++i) {
                if (!req["parent_checkpoint_ids"][i].isStr() || !store.checkpoints.count(req["parent_checkpoint_ids"][i].get_str())) ready = false;
            }
        }
        return Rec(result, id, ready ? "RECONCILED" : "CONFLICT");
    }
    if (method == "lab.operation_get") {
        const auto it = store.operations.find(RequireStr(req, "operation_id"));
        if (it == store.operations.end()) return Fail(err, "BTX035_NOT_FOUND");
        FillOp(result, it->second);
        return true;
    }
    if (method == "lab.operation_cancel") {
        if (!Expect(store, req, "cancel|" + RequireStr(req, "operation_id"), err)) return false;
        const std::string op_id = RequireStr(req, "operation_id");
        auto it = store.operations.find(op_id);
        if (it == store.operations.end()) return Fail(err, "BTX035_NOT_FOUND");
        store.gate->CancelLogical(op_id);
        it->second.state = "CANCELLED";
        result.pushKV("state", "CANCELLED");
        result.pushKV("physical_disposition", it->second.physical);
        result.pushKV("generation", std::to_string(it->second.generation));
        result.pushKV("lease_count", "0");
        result.pushKV("quiescence_evidence_id", UniValue());
        return true;
    }
    if (method == "lab.admission_evaluate") {
        if (!Expect(store, req, "admit|" + RequireStr(req, "model_ref"), err)) return false;
        std::vector<std::string> roots;
        if (req["evidence_ids"].isArray()) {
            for (size_t i = 0; i < req["evidence_ids"].size(); ++i) {
                if (req["evidence_ids"][i].isStr()) roots.push_back(req["evidence_ids"][i].get_str());
            }
        }
        const AdmissionResult decision = Admit(Assurance::PUBLIC_STRICT, false, {}, 0, "", "", roots, false);
        store.admissions[id] = decision.admitted ? "ADMITTED" : decision.code;
        return OpOut(store, result, id, decision.admitted ? "COMPLETE" : "FAILED", "STOPPED_QUIESCENT");
    }
    if (method == "lab.model_host") {
        if (!Expect(store, req, "host|" + RequireStr(req, "model_ref"), err)) return false;
        const std::string admission = RequireStr(req, "admission_id");
        const auto it = store.admissions.find(admission);
        if (it == store.admissions.end() || it->second != "ADMITTED") return Fail(err, "BTX035_ADMISSION");
        store.hosted.insert(RequireStr(req, "model_ref"));
        result.pushKV("hosted", true);
        result.pushKV("admission_id", admission);
        return true;
    }
    if (method == "lab.model_unhost") {
        if (!Expect(store, req, "unhost|" + RequireStr(req, "model_ref"), err)) return false;
        result.pushKV("unhosted", store.hosted.erase(RequireStr(req, "model_ref")) > 0);
        return true;
    }
    if (method == "lab.action_prepare") {
        if (!Expect(store, req, "prepare|" + RequireStr(req, "intent_id"), err)) return false;
        const std::string intent = RequireStr(req, "intent_id");
        if (!HexN(intent, 96)) return Fail(err, "BTX035_SCHEMA");
        ActionPermit permit;
        permit.permit_id = Id96("permit|" + intent + "|" + RequireStr(req, "idempotency_key"));
        permit.grant_id = intent;
        permit.generation = 1;
        permit.effect = "lab.action";
        permit.not_after_ms = 32503680000000LL;
        permit.parameter_fingerprint = Id96("fp|" + intent);
        if (!store.gate->Issue(permit, err)) return false;
        IssuedPermit row;
        row.effect = permit.effect;
        row.generation = permit.generation;
        row.fingerprint = permit.parameter_fingerprint;
        row.not_after_ms = permit.not_after_ms;
        store.permit_rows[permit.permit_id] = row;
        store.grants[intent] = "ALLOW";
        return Rec(result, permit.permit_id, "PREPARED");
    }
    if (method == "lab.action_execute") {
        if (!Expect(store, req, "exec|" + RequireStr(req, "one_use_permit"), err)) return false;
        const std::string permit_id = RequireStr(req, "one_use_permit");
        const auto it = store.permit_rows.find(permit_id);
        if (it == store.permit_rows.end()) return Fail(err, "BTX035_PERMIT");
        const IssuedPermit& row = it->second;
        if (!store.gate->Consume(permit_id, row.effect, row.generation, 1, row.fingerprint, err)) return false;
        return Rec(result, Id96("exec|" + permit_id), "EXECUTED");
    }
    if (method == "lab.runtime_bind") {
        if (!Expect(store, req, "bind|" + RequireStr(req, "release_id"), err)) return false;
        const std::string binding = Id96("bind|" + RequireStr(req, "release_id") + "|" + RequireStr(req, "environment_ref"));
        store.bindings[binding] = RequireStr(req, "assurance");
        return Rec(result, binding, "BOUND");
    }
    if (method == "lab.runtime_pause") {
        if (!Expect(store, req, "pause|" + RequireStr(req, "runtime_binding_id"), err)) return false;
        const std::string binding = RequireStr(req, "runtime_binding_id");
        if (!store.bindings.count(binding)) return Fail(err, "BTX035_NOT_FOUND");
        store.gate->Pause();
        store.paused_bindings.insert(binding);
        result.pushKV("state", "PAUSED");
        result.pushKV("receipt_id", Id96("pause|" + binding));
        return true;
    }
    if (method == "lab.runtime_resume") {
        if (!Expect(store, req, "resume|" + RequireStr(req, "runtime_binding_id"), err)) return false;
        const std::string binding = RequireStr(req, "runtime_binding_id");
        if (!store.paused_bindings.count(binding)) return Fail(err, "BTX035_NOT_FOUND");
        if (!store.grants.count(RequireStr(req, "new_grant_id"))) return Fail(err, "BTX035_PERMIT");
        store.paused_bindings.erase(binding);
        if (store.paused_bindings.empty()) store.gate->Resume();
        return Rec(result, Id96("resume|" + binding), "RESUMED");
    }
    if (method == "lab.journal_checkpoint") {
        if (!Expect(store, req, "cp|" + RequireStr(req, "stream_id"), err)) return false;
        const std::string cp = Id96("cp|" + RequireStr(req, "stream_id") + "|" + RequireStr(req, "through_sequence"));
        store.checkpoints[cp] = RequireStr(req, "stream_id");
        return Rec(result, cp, "CHECKPOINT");
    }
    if (method == "lab.journal_proof") {
        const std::string cp = RequireStr(req, "checkpoint_id");
        if (!store.checkpoints.count(cp)) return Fail(err, "BTX035_NOT_FOUND");
        result.pushKV("proof_ref", RefOf("POLICY_BUNDLE", cp));
        result.pushKV("event_ref", UniValue());
        return true;
    }
    if (method == "lab.journal_witness") {
        if (!Expect(store, req, "wit|" + RequireStr(req, "checkpoint_id"), err)) return false;
        if (!store.checkpoints.count(RequireStr(req, "checkpoint_id"))) return Fail(err, "BTX035_NOT_FOUND");
        return Rec(result, Id96("wit|" + RequireStr(req, "checkpoint_id")), "WITNESSED");
    }
    if (method == "lab.incident_publish") {
        if (!Expect(store, req, "inc|" + RequireStr(req, "incident_id"), err)) return false;
        if (!HexN(RequireStr(req, "incident_id"), 96)) return Fail(err, "BTX035_SCHEMA");
        store.records[RequireStr(req, "incident_id")] = req.write();
        return Rec(result, RequireStr(req, "incident_id"), "PUBLISHED");
    }
    if (method == "lab.realm_create") {
        if (!Expect(store, req, "realm|" + RequireStr(req, "name"), err)) return false;
        const std::string realm = Id96("realm|" + RequireStr(req, "name") + "|" + RequireStr(req, "nonce"));
        const std::string policy = Id96("policy|" + realm);
        if (!store.rollout.SetPolicy(realm, 1, {policy}, err)) return false;
        store.realms[realm] = policy;
        result.pushKV("realm_id", realm);
        result.pushKV("policy_id", policy);
        return true;
    }
    if (method == "lab.realm_policy_apply") {
        if (!Expect(store, req, "policy|" + RequireStr(req, "policy_id"), err)) return false;
        bool known = false;
        for (const auto& item : store.realms) {
            if (item.second == RequireStr(req, "policy_id")) known = true;
        }
        if (!known) return Fail(err, "BTX035_NOT_FOUND");
        return Rec(result, RequireStr(req, "policy_id"), "APPLIED");
    }
    if (method == "lab.realm_status") {
        const std::string realm = RequireStr(req, "realm_id");
        const auto it = store.realms.find(realm);
        if (it == store.realms.end()) return Fail(err, "BTX035_NOT_FOUND");
        result.pushKV("policy_id", it->second);
        result.pushKV("epoch", std::to_string(store.rollout.RingGeneration(realm)));
        result.pushKV("public_connectivity", "DISABLED");
        result.pushKV("revocation_age_ms", "0");
        result.pushKV("authority_state", "LOCAL");
        return true;
    }
    if (method == "lab.promotion_prepare") {
        if (!Expect(store, req, "promo|" + RequireStr(req, "source_realm_id"), err)) return false;
        if (!store.realms.count(RequireStr(req, "source_realm_id")) || !store.realms.count(RequireStr(req, "destination_realm_id"))) {
            return Fail(err, "BTX035_REALM");
        }
        store.records[id] = req.write();
        return Rec(result, id, "PREPARED");
    }
    if (method == "lab.promotion_attest") {
        if (!Expect(store, req, "promo-att|" + RequireStr(req, "promotion_id"), err)) return false;
        if (!store.records.count(RequireStr(req, "promotion_id"))) return Fail(err, "BTX035_NOT_FOUND");
        return Rec(result, Id96("patt|" + RequireStr(req, "promotion_id")), "ATTESTED");
    }
    if (method == "lab.channel_publish") {
        if (!Expect(store, req, "chan|" + RequireStr(req, "update_id"), err)) return false;
        UniValue envelope;
        if (!LoadEnvelope(store, RequireStr(req, "update_id"), envelope, err)) return false;
        const UniValue& body = envelope["body"];
        if (body["record_type"].get_str() != "ChannelUpdate") return Fail(err, "BTX035_RECORD_TYPE");
        const UniValue& payload = body["payload"];
        if (!payload.isObject() || !payload.exists("realm_id") || !payload["realm_id"].isStr() ||
            !payload.exists("channel") || !payload["channel"].isStr() ||
            !payload.exists("sequence") || !payload["sequence"].isStr()) {
            return Fail(err, "BTX035_SCHEMA");
        }
        int64_t sequence = 0;
        if (!ParseDec(payload["sequence"].get_str(), sequence)) return Fail(err, "BTX035_SCHEMA");
        const std::string key = payload["realm_id"].get_str() + "\n" + payload["channel"].get_str();
        if (!store.rollout.PublishChannel(key, sequence, RequireStr(req, "update_id"), err)) return false;
        return Rec(result, RequireStr(req, "update_id"), "PUBLISHED");
    }
    if (method == "lab.channel_get") {
        const std::string key = RequireStr(req, "realm_id") + "\n" + RequireStr(req, "channel");
        const int64_t sequence = store.rollout.ChannelSequence(key);
        const std::string body = store.rollout.ChannelBody(key);
        if (sequence < 0 || body.empty()) {
            result.pushKV("update_id", UniValue());
            result.pushKV("sequence", "0");
            result.pushKV("state", "UNKNOWN");
            return true;
        }
        result.pushKV("update_id", body);
        result.pushKV("sequence", std::to_string(sequence));
        result.pushKV("state", "CURRENT");
        return true;
    }
    if (method == "lab.rollout_start" || method == "lab.rollout_rollback") {
        if (!Expect(store, req, "roll|" + RequireStr(req, "plan_id"), err)) return false;
        const std::string plan = RequireStr(req, "plan_id");
        if (!HexN(plan, 96)) return Fail(err, "BTX035_SCHEMA");
        if (method == "lab.rollout_start") {
            store.plans[plan] = "PLANNED";
            return OpOut(store, result, id, "COMPLETE", "STOPPED_QUIESCENT");
        }
        if (!store.plans.count(plan)) return OpOut(store, result, id, "FAILED", "STOPPED_QUIESCENT");
        if (!store.records.count(RequireStr(req, "new_channel_update_id"))) return OpOut(store, result, id, "FAILED", "STOPPED_QUIESCENT");
        store.plans[plan] = "ROLLED_BACK";
        return OpOut(store, result, id, "COMPLETE", "STOPPED_QUIESCENT");
    }
    if (method == "lab.rollout_pause") {
        if (!Expect(store, req, "rpause|" + RequireStr(req, "plan_id"), err)) return false;
        const std::string plan = RequireStr(req, "plan_id");
        if (!store.plans.count(plan)) return Fail(err, "BTX035_NOT_FOUND");
        store.plans[plan] = "PAUSED";
        const uint64_t epoch = ++store.pause_epoch[plan];
        result.pushKV("paused", true);
        result.pushKV("pause_epoch", std::to_string(epoch));
        UniValue counts = Counts(0, 0);
        counts.pushKV("paused", "1");
        result.pushKV("counts", counts);
        result.pushKV("snapshot_id", Id96("roll|" + plan + "|" + std::to_string(epoch)));
        result.pushKV("endpoint_query_method", "lab.rollout_endpoints");
        return true;
    }
    if (method == "lab.rollout_status" || method == "lab.rollout_endpoints") {
        const std::string plan = RequireStr(req, "plan_id");
        if (!store.plans.count(plan)) return Fail(err, "BTX035_NOT_FOUND");
        const FleetCounts fleet = store.rollout.FleetStatus();
        UniValue counts = Counts(static_cast<uint64_t>(fleet.healthy + fleet.unhealthy + fleet.unknown), static_cast<uint64_t>(fleet.unknown));
        counts.pushKV("healthy", std::to_string(fleet.healthy));
        counts.pushKV("failed", std::to_string(fleet.unhealthy));
        if (store.plans[plan] == "PAUSED") counts.pushKV("paused", "1");
        if (method == "lab.rollout_endpoints") {
            result.pushKV("endpoints", UniValue(UniValue::VARR));
            result.pushKV("counts", counts);
            result.pushKV("target_set_root", Id96("targets|" + plan));
            result.pushKV("snapshot_id", Id96("ends|" + plan));
            result.pushKV("observed_ms", "0");
            result.pushKV("next_cursor", UniValue());
            return true;
        }
        result.pushKV("state", store.plans[plan]);
        result.pushKV("counts", counts);
        result.pushKV("target_set_root", Id96("targets|" + plan));
        result.pushKV("snapshot_id", Id96("status|" + plan));
        result.pushKV("observed_ms", "0");
        result.pushKV("freshness_limit_ms", "10000");
        result.pushKV("pause_epoch", std::to_string(store.pause_epoch[plan]));
        result.pushKV("endpoint_query_method", "lab.rollout_endpoints");
        return true;
    }
    if (method == "lab.deployment_observe") {
        if (!Expect(store, req, "obs|" + RequireStr(req, "receipt_id"), err)) return false;
        if (!HexN(RequireStr(req, "receipt_id"), 96)) return Fail(err, "BTX035_SCHEMA");
        store.records[RequireStr(req, "receipt_id")] = req.write();
        return Rec(result, RequireStr(req, "receipt_id"), "OBSERVED");
    }
    if (method == "lab.trust_reconcile") {
        if (!Expect(store, req, "trust|" + RequireStr(req, "owner_approval_id"), err)) return false;
        bool ready = req["anchor_record_ids"].isArray() && !req["anchor_record_ids"].empty();
        std::string joined;
        if (ready) {
            for (size_t i = 0; i < req["anchor_record_ids"].size(); ++i) {
                if (!req["anchor_record_ids"][i].isStr() || !store.records.count(req["anchor_record_ids"][i].get_str())) ready = false;
                else joined += req["anchor_record_ids"][i].get_str();
            }
        }
        result.pushKV("state", ready ? "RECONCILED" : "RECOVERY_REQUIRED");
        result.pushKV("reconciled_anchor_root", ready ? UniValue(Id96(joined)) : UniValue());
        return true;
    }
    if (method == "lab.bundle_export") {
        if (!Expect(store, req, "bex|" + RequireStr(req, "bundle_id"), err)) return false;
        const std::string name = RequireStr(req, "local_destination");
        if (!SafeSegment(name)) return Fail(err, "BTX035_PATH_UNSAFE");
        const std::string realm = RequireStr(req, "bundle_id");
        if (!store.realms.count(realm)) return OpOut(store, result, id, "FAILED", "STOPPED_QUIESCENT");
        if (!EnsureBundleDir(err)) return false;
        const fs::path path = BundleDir() / name.c_str();
        if (!store.rollout.WriteOfflineBundle(path, realm, err)) return false;
        return OpOut(store, result, id, "COMPLETE", "STOPPED_QUIESCENT");
    }
    if (method == "lab.bundle_import") {
        if (!Expect(store, req, "bim|" + RequireStr(req, "local_source"), err)) return false;
        const std::string name = RequireStr(req, "local_source");
        if (!SafeSegment(name)) return Fail(err, "BTX035_PATH_UNSAFE");
        int64_t minimum = 0;
        if (!ParseDec(RequireStr(req, "minimum_policy_epoch"), minimum)) return Fail(err, "BTX035_SCHEMA");
        const std::string realm = RequireStr(req, "destination_realm_id");
        if (store.rollout.RingGeneration(realm) < minimum) return Fail(err, "BTX035_POLICY");
        const fs::path path = BundleDir() / name.c_str();
        if (!store.rollout.ReadOfflineBundle(path, realm, err)) return false;
        return OpOut(store, result, id, "COMPLETE", "STOPPED_QUIESCENT");
    }
    if (method == "lab.migration_plan") {
        result.pushKV("plan_ref", RefOf("RELEASE", Id96("plan|" + RequireStr(req, "target_version"))));
        result.pushKV("legacy_model_count", "0");
        result.pushKV("requires_stop", false);
        return true;
    }
    if (method == "lab.migration_apply") {
        if (!Expect(store, req, "mig|" + RequireStr(req, "idempotency_key"), err)) return false;
        if (!req["plan_ref"].isObject()) return Fail(err, "BTX035_SCHEMA");
        store.migrations.insert(id);
        return OpOut(store, result, id, "COMPLETE", "STOPPED_QUIESCENT");
    }
    if (method == "lab.migration_verify") {
        const std::string mig = RequireStr(req, "migration_id");
        const bool done = store.migrations.count(mig) != 0;
        result.pushKV("state", done ? "COMPLETE" : "INCOMPLETE");
        result.pushKV("money_unchanged", true);
        result.pushKV("source_map_ref", RefOf("RELEASE", Id96("src|" + mig)));
        return true;
    }
    if (method == "lab.economy_refs") {
        if (ResearchMaySpendWallet()) return Fail(err, "BTX035_SCOPE_DENIED");
        result.pushKV("bounty_ids", UniValue(UniValue::VARR));
        result.pushKV("agreement_ids", UniValue(UniValue::VARR));
        result.pushKV("spend_authorized", false);
        return true;
    }
    if (method == "lab.capsule_resolve") {
        const std::string capsule = RequireStr(req, "capsule_id");
        result.pushKV("plan_id", Id96("plan|" + capsule));
        UniValue missing(UniValue::VARR);
        if (!store.records.count(capsule)) {
            UniValue ref = RefOf("RECORD", HexN(capsule, 96) ? capsule : Id96(capsule));
            ref.pushKV("record_type", "ResearchCapsule");
            missing.push_back(ref);
        }
        result.pushKV("missing_refs", missing);
        result.pushKV("untrusted_refs", UniValue(UniValue::VARR));
        result.pushKV("estimated_bytes", "0");
        return true;
    }
    if (method == "lab.state_get") {
        const std::string key = RequireStr(req, "target_key");
        if (!HexN(key, 96)) return Fail(err, "BTX035_SCHEMA");
        const bool exists = store.records.count(key) || store.operations.count(key) || store.identities.count(key) ||
                            store.repos.count(key) || store.realms.count(key) || store.workspace_backend.count(key) ||
                            store.plans.count(key) || store.snapshots.count(key) || store.checkpoints.count(key) ||
                            store.grants.count(key) || store.bindings.count(key) || store.workspace_backend.count(key);
        result.pushKV("target_kind", RequireStr(req, "target_kind"));
        result.pushKV("target_key", key);
        result.pushKV("exists", exists);
        result.pushKV("generation", exists ? "1" : "0");
        result.pushKV("value_id", exists ? UniValue(key) : UniValue());
        result.pushKV("policy_epoch", "0");
        result.pushKV("state_token", UniValue());
        return true;
    }
    if (method == "lab.audit_coverage") {
        result.pushKV("complete_path", false);
        result.pushKV("accepted_attestation_ids", UniValue(UniValue::VARR));
        UniValue gaps(UniValue::VARR);
        gaps.push_back("BTX035_NOT_FOUND");
        result.pushKV("gaps", gaps);
        result.pushKV("mode", "LOCAL_REPLAY");
        return true;
    }
    return Fail(err, "BTX035_NOT_IMPLEMENTED");
}
