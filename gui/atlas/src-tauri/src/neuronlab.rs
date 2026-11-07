//! Private, bounded research-worker bridge. No browser RPC cookie or wallet key.
//! Native compilation/platform qualification is tracked separately in the report.
use crate::host::{authorize, blocking, confirm, lock, pick, pick_folder, Host};
use atlas_core::{files, strict_json, Error, Result};
use serde_json::{json, Value};
use std::{
    collections::BTreeSet,
    io::{BufRead, BufReader, Read, Write},
    path::{Path, PathBuf},
    process::{Command, Stdio},
    sync::{
        atomic::{AtomicU64, AtomicUsize, Ordering},
        Arc, Mutex, OnceLock,
    },
    time::{Duration, Instant},
};
use tauri::{AppHandle, Emitter, Manager, State, WebviewWindow};

const INPUT_LIMIT: usize = 262_144;
const OUTPUT_LIMIT: usize = 8_388_608;
const READS: &[&str] = &[
    "status",
    "list",
    "models",
    "profile",
    "events",
    "receipts",
    "binding",
    "router_preview",
    "adapter_catalog",
    "embeddings_list",
    "embeddings_page",
    "session_list",
    "session_get",
    "session_operation",
    "trace_get",
    "qualification_list",
    "annotation_get",
    "sweep_get",
];
const COMPUTE: &[&str] = &[
    "compare",
    "generate",
    "study_run",
    "direction",
    "lens",
    "run_router",
    "embeddings_import",
    "embeddings_capture",
    "embeddings_neighbors",
    "session_turn",
    "trace_chunk",
    "trace_path",
    "trace_interaction",
    "model_qualify",
    "activation_scan",
    "measured_lens",
    "connection_cone",
    "sweep_chunk",
];
const ACTIONS: &[&str] = &[
    "status",
    "list",
    "models",
    "profile",
    "events",
    "receipts",
    "binding",
    "router_preview",
    "initialize",
    "register",
    "create",
    "import",
    "export",
    "trust",
    "revoke",
    "activate",
    "deactivate",
    "compare",
    "generate",
    "direction",
    "lens",
    "study_plan",
    "study_run",
    "evaluate",
    "save_router",
    "run_router",
    "import_evidence",
    "adapter_catalog",
    "embeddings_list",
    "embeddings_page",
    "session_list",
    "session_get",
    "session_operation",
    "trace_get",
    "qualification_list",
    "annotation_get",
    "sweep_get",
    "embeddings_import",
    "embeddings_capture",
    "embeddings_neighbors",
    "session_turn",
    "trace_chunk",
    "trace_path",
    "trace_interaction",
    "model_qualify",
    "activation_scan",
    "measured_lens",
    "connection_cone",
    "sweep_chunk",
    "session_create",
    "session_fork",
    "session_recover",
    "trace_plan",
    "annotation_set",
    "sweep_plan",
];
static CANCEL: AtomicU64 = AtomicU64::new(0);
static PROCESSES: AtomicUsize = AtomicUsize::new(0);
static HEAVY: AtomicUsize = AtomicUsize::new(0);
static CONFIG: OnceLock<Mutex<Option<Runner>>> = OnceLock::new();
#[derive(Clone)]
struct Runner {
    python: PathBuf,
    python_target: PathBuf,
    entry: PathBuf,
    store: PathBuf,
    python_hash: String,
    manifest_hash: String,
}
struct Slot {
    heavy: bool,
}
impl Slot {
    fn acquire(heavy: bool) -> Result<Self> {
        if PROCESSES.fetch_add(1, Ordering::SeqCst) >= 3 {
            PROCESSES.fetch_sub(1, Ordering::SeqCst);
            return Err(Error::new(
                "LAB_BUSY",
                "At most three research operations may run",
            ));
        }
        if heavy && HEAVY.fetch_add(1, Ordering::SeqCst) >= 1 {
            HEAVY.fetch_sub(1, Ordering::SeqCst);
            PROCESSES.fetch_sub(1, Ordering::SeqCst);
            return Err(Error::new(
                "LAB_COMPUTE_BUSY",
                "Only one local scientific compute request may run",
            ));
        }
        Ok(Self { heavy })
    }
}
impl Drop for Slot {
    fn drop(&mut self) {
        PROCESSES.fetch_sub(1, Ordering::SeqCst);
        if self.heavy {
            HEAVY.fetch_sub(1, Ordering::SeqCst);
        }
    }
}
fn config() -> &'static Mutex<Option<Runner>> {
    CONFIG.get_or_init(|| Mutex::new(None))
}
fn err(code: &str, msg: &str) -> Error {
    Error::new(code, msg)
}
fn string<'a>(v: &'a Value, k: &str) -> Result<&'a str> {
    v.get(k)
        .and_then(Value::as_str)
        .ok_or_else(|| err("LAB_SCHEMA", "Missing text field"))
}
fn digest(value: &str) -> Result<()> {
    if value.len() != 96
        || !value
            .bytes()
            .all(|b| b.is_ascii_digit() || (b'a'..=b'f').contains(&b))
    {
        return Err(err("LAB_ID", "Expected 96 lowercase hex characters"));
    }
    Ok(())
}
fn only_keys(p: &Value, allowed: &[&str]) -> Result<()> {
    let o = p
        .as_object()
        .ok_or_else(|| err("LAB_SCHEMA", "Object payload required"))?;
    if o.keys().any(|k| !allowed.contains(&k.as_str())) {
        return Err(err("LAB_SCHEMA", "Unknown payload field"));
    }
    Ok(())
}
fn source_manifest(entry: &Path) -> Result<(String, Value)> {
    if entry.file_name().and_then(|s| s.to_str()) != Some("worker.py") {
        return Err(err(
            "LAB_RUNNER",
            "Select the installed worker.py entry point",
        ));
    }
    let parent = entry
        .parent()
        .ok_or_else(|| err("LAB_RUNNER", "No source root"))?;
    let manifest = files::read_bounded(&parent.join("code-manifest.json"), 131072)?;
    let parsed = strict_json::parse(&manifest, 131072)?;
    if parsed.get("schema").and_then(Value::as_str) != Some("btx.neuronlab.code-manifest/1") {
        return Err(err("LAB_MANIFEST", "Unknown source-manifest schema"));
    }
    Ok((atlas_core::hash(&manifest), parsed))
}
/// The selected interpreter path, made absolute, and the file it resolves to.
/// A venv's python is a symlink to the base interpreter, and Python finds the
/// venv (pyvenv.cfg) from the path it was started with, so the selected path is
/// what runs. The resolved file is what is hashed.
fn interpreter(selected: &Path) -> Result<(PathBuf, PathBuf)> {
    let python = std::path::absolute(selected)
        .map_err(|_| err("LAB_PYTHON", "Cannot resolve interpreter"))?;
    let target = python
        .canonicalize()
        .map_err(|_| err("LAB_PYTHON", "Cannot resolve interpreter"))?;
    Ok((python, target))
}
fn verify_interpreter(python: &Path, target: &Path, python_hash: &str) -> Result<()> {
    if python.canonicalize().ok().as_deref() != Some(target)
        || atlas_core::hash(&files::read_bounded(target, 268_435_456)?) != python_hash
    {
        return Err(err(
            "LAB_INTERPRETER_CHANGED",
            "Research interpreter changed; configure again",
        ));
    }
    Ok(())
}
fn verify_sources(r: &Runner) -> Result<()> {
    verify_interpreter(&r.python, &r.python_target, &r.python_hash)?;
    let (hash, m) = source_manifest(&r.entry)?;
    if hash != r.manifest_hash {
        return Err(err(
            "LAB_SOURCE_CHANGED",
            "Research source manifest changed; configure again",
        ));
    }
    let root = r
        .entry
        .parent()
        .ok_or_else(|| err("LAB_RUNNER", "No source root"))?;
    let entries = m
        .get("files")
        .and_then(Value::as_object)
        .ok_or_else(|| err("LAB_MANIFEST", "Missing source file hashes"))?;
    if entries.is_empty() || entries.len() > 128 {
        return Err(err("LAB_MANIFEST", "Invalid source count"));
    }
    let mut allowed = BTreeSet::new();
    for (name, value) in entries {
        if name.contains("..")
            || name.contains('\\')
            || name.starts_with('/')
            || !name.ends_with(".py")
        {
            return Err(err("LAB_MANIFEST", "Unsafe source path"));
        }
        let path = root.join(name);
        let metadata =
            std::fs::symlink_metadata(&path).map_err(|_| err("LAB_SOURCE", "Source missing"))?;
        if !metadata.is_file() || metadata.file_type().is_symlink() {
            return Err(err("LAB_SOURCE", "Only regular source files are accepted"));
        }
        if Some(atlas_core::hash(&files::read_bounded(&path, 1_048_576)?).as_str())
            != value.as_str()
        {
            return Err(err("LAB_SOURCE_CHANGED", "Research module hash mismatch"));
        }
        allowed.insert(name.clone());
    }
    // Prevent a .so/.pyc or unlisted .py shadowing a reviewed package module.
    for entry in std::fs::read_dir(root.join("btx_neuronlab"))
        .map_err(|_| err("LAB_SOURCE", "Package directory unavailable"))?
    {
        let e = entry.map_err(|_| err("LAB_SOURCE", "Cannot enumerate installed package"))?;
        let name = format!("btx_neuronlab/{}", e.file_name().to_string_lossy());
        if !allowed.contains(&name) {
            return Err(err(
                "LAB_SOURCE_UNREVIEWED",
                "Unlisted files or bytecode in the package; use a clean reviewed installation",
            ));
        }
    }
    Ok(())
}
fn spawn_reader<T: Read + Send + 'static>(
    reader: T,
    max: usize,
) -> std::sync::mpsc::Receiver<std::io::Result<Vec<u8>>> {
    let (tx, rx) = std::sync::mpsc::sync_channel(1);
    std::thread::spawn(move || {
        let mut out = Vec::new();
        let result = reader
            .take((max + 1) as u64)
            .read_to_end(&mut out)
            .map(|_| out);
        let _ = tx.send(result);
    });
    rx
}
// Streaming frames are private, bounded display data, never commands. Only the
// authorized main window receives them. The final result still has a release fence.
fn spawn_frames<T: Read + Send + 'static>(
    reader: T,
    app: Option<AppHandle>,
    operation: String,
    epoch: u64,
) -> std::sync::mpsc::Receiver<std::io::Result<Vec<u8>>> {
    let (tx, rx) = std::sync::mpsc::sync_channel(1);
    std::thread::spawn(move || {
        let result = (|| -> std::io::Result<Vec<u8>> {
            let mut input = BufReader::new(reader);
            let mut final_frame: Option<Vec<u8>> = None;
            let mut events = 0usize;
            loop {
                let mut line = Vec::new();
                loop {
                    let buf = input.fill_buf()?;
                    if buf.is_empty() {
                        break;
                    }
                    let take = buf
                        .iter()
                        .position(|b| *b == b'\n')
                        .map(|i| i + 1)
                        .unwrap_or(buf.len());
                    if line.len() + take > OUTPUT_LIMIT + 1 {
                        return Err(std::io::Error::other("research frame size exceeded"));
                    }
                    let ended = buf[take - 1] == b'\n';
                    line.extend_from_slice(&buf[..take]);
                    input.consume(take);
                    if ended {
                        break;
                    }
                }
                if line.is_empty() {
                    break;
                }
                if final_frame.is_some() {
                    return Err(std::io::Error::other("frame after final response"));
                }
                let v = strict_json::parse(&line, OUTPUT_LIMIT)
                    .map_err(|_| std::io::Error::other("invalid research frame"))?;
                if let Some(event) = v.get("event") {
                    if line.len() > 65536
                        || v.as_object().map(|o| o.len()) != Some(2)
                        || v.get("operation_id").and_then(Value::as_str) != Some(operation.as_str())
                        || !event.is_object()
                    {
                        return Err(std::io::Error::other("invalid progress envelope"));
                    }
                    events += line.len();
                    if events > 4_194_304 {
                        return Err(std::io::Error::other("progress budget exceeded"));
                    }
                    if CANCEL.load(Ordering::SeqCst) != epoch {
                        return Err(std::io::Error::other("progress cancelled"));
                    }
                    if let Some(ref host) = app {
                        host.emit_to(
                            "main",
                            "neuronlab-progress",
                            json!({"operation_id":operation,"event":event}),
                        )
                        .map_err(|_| std::io::Error::other("progress delivery failed"))?;
                    }
                } else {
                    if v.get("ok").and_then(Value::as_bool).is_none() {
                        return Err(std::io::Error::other("missing final status"));
                    }
                    final_frame = Some(line);
                }
            }
            final_frame.ok_or_else(|| std::io::Error::other("missing final research response"))
        })();
        let _ = tx.send(result);
    });
    rx
}
fn run(r: &Runner, action: &str, payload: Value, operation_id: &str) -> Result<Value> {
    run_stream(r, action, payload, operation_id, None)
}
fn run_stream(
    r: &Runner,
    action: &str,
    payload: Value,
    operation_id: &str,
    app: Option<AppHandle>,
) -> Result<Value> {
    let _slot = Slot::acquire(COMPUTE.contains(&action))?;
    verify_sources(r)?;
    let start_epoch = CANCEL.load(Ordering::SeqCst);
    let mut raw =
        serde_json::to_vec(&json!({"action":action,"payload":payload,"operation_id":operation_id}))
            .map_err(|_| err("LAB_JSON", "Cannot serialize research request"))?;
    if raw.len() > INPUT_LIMIT {
        return Err(err("LAB_SIZE", "Research request exceeds 256 KiB"));
    }
    raw.push(b'\n');
    let mut cmd = Command::new(&r.python);
    cmd.arg("-I")
        .arg("-B")
        .arg("-X")
        .arg("utf8")
        .arg(&r.entry)
        .arg("--store")
        .arg(&r.store)
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .env_clear();
    for k in [
        "HOME",
        "USERPROFILE",
        "APPDATA",
        "LOCALAPPDATA",
        "SystemRoot",
        "WINDIR",
        "PATH",
        "TMPDIR",
        "TEMP",
        "TMP",
    ] {
        if let Some(v) = std::env::var_os(k) {
            cmd.env(k, v);
        }
    }
    if app.is_some() {
        cmd.arg("--stream");
    }
    cmd.env("HF_HUB_OFFLINE", "1")
        .env("TRANSFORMERS_OFFLINE", "1")
        .env("OMP_NUM_THREADS", "2");
    let mut child = cmd.spawn().map_err(|_| {
        err(
            "LAB_LAUNCH",
            "Cannot start the approved research interpreter",
        )
    })?;
    let mut stdin = child
        .stdin
        .take()
        .ok_or_else(|| err("LAB_PIPE", "Missing private stdin"))?;
    let stdout = spawn_frames(
        child
            .stdout
            .take()
            .ok_or_else(|| err("LAB_PIPE", "Missing private stdout"))?,
        app,
        operation_id.to_owned(),
        start_epoch,
    );
    let stderr = spawn_reader(
        child
            .stderr
            .take()
            .ok_or_else(|| err("LAB_PIPE", "Missing private stderr"))?,
        65536,
    );
    let (tx, writer) = std::sync::mpsc::sync_channel(1);
    std::thread::spawn(move || {
        let result = stdin.write_all(&raw);
        drop(stdin);
        let _ = tx.send(result);
    });
    let deadline =
        Instant::now() + Duration::from_secs(if COMPUTE.contains(&action) { 900 } else { 60 });
    let exit = loop {
        if CANCEL.load(Ordering::SeqCst) != start_epoch || Instant::now() >= deadline {
            let _ = child.kill();
            let _ = child.wait();
            return Err(Error::unknown("Research job stopped or timed out; inspect its operation ID before retrying a mutation"));
        }
        match child.try_wait() {
            Ok(Some(s)) => break s,
            Ok(None) => std::thread::sleep(Duration::from_millis(20)),
            Err(_) => {
                let _ = child.kill();
                let _ = child.wait();
                return Err(Error::unknown("Research worker state is unknown"));
            }
        }
    };
    writer
        .recv_timeout(Duration::from_secs(2))
        .map_err(|_| Error::unknown("Worker input completion unknown"))?
        .map_err(|_| Error::unknown("Worker request delivery failed"))?;
    let out = stdout
        .recv_timeout(Duration::from_secs(2))
        .map_err(|_| Error::unknown("Worker output did not close"))?
        .map_err(|_| err("LAB_OUTPUT", "Cannot read worker result"))?;
    let diagnostics = stderr
        .recv_timeout(Duration::from_secs(2))
        .map_err(|_| err("LAB_OUTPUT", "Worker diagnostics did not close"))?
        .map_err(|_| err("LAB_OUTPUT", "Cannot read diagnostics"))?;
    if out.len() > OUTPUT_LIMIT || diagnostics.len() > 65536 {
        return Err(err("LAB_OUTPUT_LIMIT", "Worker output exceeded its bounds"));
    }
    if CANCEL.load(Ordering::SeqCst) != start_epoch {
        return Err(err("LAB_REVOKED", "Result release cancelled"));
    }
    let response = strict_json::parse(&out, OUTPUT_LIMIT)?;
    if response.get("ok").and_then(Value::as_bool) != Some(true) {
        return Err(Error::new(
            response
                .pointer("/error/code")
                .and_then(Value::as_str)
                .unwrap_or("LAB_FAILURE"),
            atlas_core::safe_text(
                response
                    .pointer("/error/message")
                    .and_then(Value::as_str)
                    .unwrap_or("Research worker refused request"),
                1024,
            ),
        ));
    }
    if !exit.success() {
        return Err(err("LAB_EXIT", "Worker exited unsuccessfully"));
    }
    response
        .get("result")
        .cloned()
        .ok_or_else(|| err("LAB_RESULT", "Worker result missing"))
}
/// The native review of a research write. `operators` is the worker's signed
/// `profile.operations` for compare, generate and activate.
fn intervention_review(
    action: &str,
    operation_id: &str,
    p: &Value,
    operators: Option<&Value>,
) -> Result<String> {
    let pretty = serde_json::to_string_pretty(p)
        .map_err(|_| err("LAB_JSON", "Review serialization failed"))?;
    let mut review = format!("Research action: {action}\nOperation ID: {operation_id}\n\n{pretty}\n\nNo wallet/host-tool authority is added. Profile signatures do not certify model behavior.");
    if let Some(operators) = operators {
        review.push_str(&format!(
            "\n\nExact signed operators:\n{}",
            serde_json::to_string_pretty(operators).unwrap_or_default()
        ));
    }
    if review.len() > 1200 {
        review = compact_review(action, operation_id, p, &pretty, operators);
    }
    Ok(review)
}
fn compact_review(
    action: &str,
    operation_id: &str,
    payload: &Value,
    pretty: &str,
    operators: Option<&Value>,
) -> String {
    let preview: String = pretty.chars().take(400).collect();
    let digest = atlas_core::hash(pretty.as_bytes());
    let title = payload
        .get("title")
        .and_then(Value::as_str)
        .map(|title| format!("Title: {title}\n"))
        .unwrap_or_default();
    let operators = operators.map(operator_lines).unwrap_or_default();
    format!(
        "Research action: {action}\nOperation ID: {operation_id}\n{title}\n{preview}\n\nSHA-256: {digest}{operators}\n\nApproval still covers the exact payload and the dialog omitted the rest so the buttons stay on screen."
    )
}
/// Signed operators shown in a compact review: their count, the SHA-256 of their
/// JSON, and one line for each of the first eight.
fn operator_lines(operators: &Value) -> String {
    const SHOWN: usize = 8;
    let json = serde_json::to_string(operators).unwrap_or_default();
    let list = operators.as_array().map(Vec::as_slice).unwrap_or_default();
    let mut out = format!(
        "\n\nExact signed operators: {}, SHA-256 {}",
        list.len(),
        atlas_core::hash(json.as_bytes())
    );
    let text = |o: &Value, key: &str| match o.get(key) {
        Some(Value::String(s)) => atlas_core::safe_text(s, 64),
        Some(v) => atlas_core::safe_text(&v.to_string(), 64),
        None => "?".to_owned(),
    };
    for o in list.iter().take(SHOWN) {
        let indices = o.get("indices").and_then(Value::as_array);
        let mut units = indices
            .map(|a| a.iter().take(8).map(Value::to_string).collect::<Vec<_>>())
            .unwrap_or_default()
            .join(",");
        if let Some(rest) = indices
            .map(|a| a.len().saturating_sub(8))
            .filter(|n| *n > 0)
        {
            units.push_str(&format!(",+{rest}"));
        }
        out.push_str(&format!(
            "\n- {}: {} layer {} units [{units}] {} ppm {} phase {} tokens {}..{}",
            text(o, "id"),
            text(o, "kind"),
            text(o, "layer"),
            text(o, "coefficient_ppm"),
            text(o, "normalization"),
            text(o, "phase"),
            text(o, "token_start"),
            text(o, "token_end"),
        ));
    }
    if list.len() > SHOWN {
        out.push_str(&format!(
            "\n- {} more operators, covered by the SHA-256 above",
            list.len() - SHOWN
        ));
    }
    out
}

fn uri(p: &Value) -> Result<String> {
    let s = string(p, "uri")?;
    if s.len() > 4096
        || !s.starts_with("btx://")
        || (s.contains('?') || s.contains('#'))
        || s.chars().any(|c| c.is_control() || c.is_whitespace())
    {
        return Err(err("LAB_URI", "Expected canonical BTX resource URI"));
    }
    Ok(s.to_owned())
}

#[tauri::command]
pub fn atlas_neuronlab_cancel(window: WebviewWindow) -> Result<Value> {
    authorize(&window)?;
    let epoch = CANCEL.fetch_add(1, Ordering::SeqCst) + 1;
    Ok(json!({"cancel_epoch":epoch,"scope":"research-worker-only; monetary node untouched"}))
}

#[tauri::command]
pub async fn atlas_neuronlab(
    window: WebviewWindow,
    app: AppHandle,
    state: State<'_, Arc<Host>>,
    action: String,
    payload_json: String,
    operation_id: String,
) -> Result<Value> {
    authorize(&window)?;
    if operation_id.len() > 96
        || operation_id.is_empty()
        || !operation_id
            .bytes()
            .all(|c| c.is_ascii_alphanumeric() || c == b'-' || c == b'_')
    {
        return Err(err("LAB_OPERATION_ID", "Invalid operation ID"));
    }
    let mut p = strict_json::parse(payload_json.as_bytes(), INPUT_LIMIT)?;
    if !p.is_object() {
        return Err(err("LAB_SCHEMA", "Object payload required"));
    }
    let h = state.inner().clone();
    blocking(move||{
  if action=="configure"{
   only_keys(&p,&[])?;
   let(python,python_target)=interpreter(&pick(&app,false,"Choose a trusted Python 3.11+ interpreter for scientific work")?)?;
   let entry=pick(&app,false,"Choose installed contrib/atlas/neuronlab/worker.py")?.canonicalize().map_err(|_|err("LAB_RUNNER","Cannot resolve worker entry"))?;
   let(hash,_)=source_manifest(&entry)?;
   let python_hash=atlas_core::hash(&files::read_bounded(&python_target,268_435_456)?);
   let store=app.path().app_local_data_dir().map_err(|_|err("LAB_STORE","No local application directory"))?.join("neuronlab");
   let r=Runner{python,python_target,entry,store,python_hash,manifest_hash:hash};verify_sources(&r)?;
   confirm(&app,"Install local scientific worker",format!("Interpreter: {}\nResolves to: {}\nSHA-256: {}\nWorker: {}\nSource manifest SHA-256: {}\nStore: {}\n\nThis explicitly trusts installed code and its scientific dependencies. It does NOT grant wallet keys or peer activation. OS sandbox and native platform qualification remain separate gates.",r.python.display(),r.python_target.display(),r.python_hash,r.entry.display(),r.manifest_hash,r.store.display()))?;
   CANCEL.fetch_add(1,Ordering::SeqCst);*lock(config())?=Some(r.clone());return run(&r,"status",json!({}),&operation_id);
  }
  let r=lock(config())?.clone().ok_or_else(||err("LAB_NOT_CONFIGURED","Configure an approved local research worker first"))?;
  if action=="publish"{
   only_keys(&p,&["profile_id"])?;let id=string(&p,"profile_id")?;digest(id)?;
   let plan=run(&r,"publish_plan",p.clone(),&format!("{operation_id}-plan"))?;
   // Reconstruct native parameters; never proxy worker-supplied RPC or arguments.
   let path=r.store.join("capsules").join(format!("{id}.safetensors"));
   confirm(&app,"Serve public intervention data",format!("Profile: {id}\nCapsule SHA-384: {}\n\nServe this PUBLIC data-only SafeTensors capsule using the connected BTX node. It is not a model checkpoint, execution grant, or spend. Search-card publication is disabled; share the resulting BTX resource URI explicitly.",plan.get("capsule_sha384").and_then(Value::as_str).unwrap_or("unknown")))?;
   let c=h.connected()?;h.recheck(&c)?;h.audit(&operation_id,"neuronlab.publish","SUBMITTED")?;
   let result=c.rpc.call("hostmodel",&json!([path.to_string_lossy(),{"pin":true,"publish":false}]),None,180)?;
   h.recheck(&c)?;h.audit(&operation_id,"neuronlab.publish","SUCCEEDED")?;
   return Ok(json!({"profile_id":id,"native_result":result,"activation_authorized":false,"automatic_spend_atoms":"0"}));
  }
  if action=="acquire"{
   only_keys(&p,&["uri"])?;let resource=uri(&p)?;
   confirm(&app,"Retrieve intervention bytes",format!("Resource: {resource}\n\nUse FREE_ONLY retrieval. This may acquire and serve bytes under your existing node storage policy. Downloading does not trust a publisher or apply neuron settings."))?;
   let c=h.connected()?;h.recheck(&c)?;h.audit(&operation_id,"neuronlab.acquire","SUBMITTED")?;
   let result=c.rpc.call("getmodel",&json!([resource,{"mode":"FREE_ONLY"}]),None,180)?;
   h.recheck(&c)?;h.audit(&operation_id,"neuronlab.acquire","ACCEPTED_NOT_COMPLETED")?;
   return Ok(json!({"native_result":result,"complete":"CHECK_NATIVE_JOB","activation_authorized":false}));
  }
  if action=="finish_acquire"{
   only_keys(&p,&["uri","profile_id"])?;let resource=uri(&p)?;let id=string(&p,"profile_id")?.to_owned();digest(&id)?;
   confirm(&app,"Verify completed intervention checkout",format!("Resource: {resource}\nExpected profile: {id}\n\nAsk the native node for its verified checkout, then verify the capsule signature and exact profile identity. Do not import incomplete or wrong bytes. No activation follows."))?;
   let c=h.connected()?;h.recheck(&c)?;let result=c.rpc.call("exportmodelpath",&json!([resource]),None,180)?;h.recheck(&c)?;
   let path=result.get("usable_runtime_root").or_else(||result.get("path")).and_then(Value::as_str).ok_or_else(||err("LAB_CHECKOUT","Native response lacks a verified checkout root"))?;
   return run(&r,"import_checkout",json!({"path":path,"profile_id":id}),&operation_id);
  }
  if !ACTIONS.contains(&action.as_str()){return Err(err("LAB_ACTION","Unknown research action"));}
  // File paths are selected here, never accepted from a generated panel or profile.
  if action=="register"{only_keys(&p,&[])?;let path=pick_folder(&app,"Select exact local SafeTensors model directory; no download or code execution")?;p=json!({"path":path.to_string_lossy()});}
  if action=="import"||action=="import_evidence"{only_keys(&p,&[])?;let path=pick(&app,false,"Select a signed research capsule/evidence file to inspect")?;p=json!({"path":path.to_string_lossy()});}
  if action=="embeddings_import"{
   only_keys(&p,&["title","method","seed"])?;
   let path=pick(&app,false,"Select a local numeric .npy embedding matrix (no pickle, no archive)")?;
   p["path"]=json!(path.to_string_lossy());
  }
  if action=="export"{only_keys(&p,&["profile_id"])?;digest(string(&p,"profile_id")?)?;let path=pick(&app,true,"Save capsule to a NEW .safetensors file")?;p["path"]=json!(path.to_string_lossy());}
  if !READS.contains(&action.as_str()){
   let mut actual=None;
   if ["compare","generate","activate"].contains(&action.as_str()){
    if let Some(id)=p.get("profile_id").and_then(Value::as_str){actual=Some(run(&r,"profile",json!({"profile_id":id}),&format!("{operation_id}-inspect"))?);}
   }
   let operators=actual.as_ref().map(|a|a.pointer("/profile/operations").unwrap_or(&Value::Null));
   let review=intervention_review(&action,&operation_id,&p,operators)?;
   confirm(&app,"Review local model intervention",review)?;
   if action=="revoke"||action=="deactivate"{CANCEL.fetch_add(1,Ordering::SeqCst);}
   h.audit(&operation_id,&format!("neuronlab.{action}"),"SUBMITTED")?;
  }
  let result=run_stream(&r,&action,p,&operation_id,Some(app.clone()))?;
  if !READS.contains(&action.as_str()){h.audit(&operation_id,&format!("neuronlab.{action}"),"SUCCEEDED")?;}
  Ok(result)
 }).await
}

#[cfg(test)]
mod tests {
    use super::*;
    fn operator(id: &str, layer: u64, indices: &[u64], coefficient_ppm: i64) -> Value {
        // The shape of web/neuronlab/contract.js operation().
        json!({"id":id,"kind":"neuron_scale","layer":layer,"indices":indices,"coefficient_ppm":coefficient_ppm,"normalization":"absolute","phase":"both","token_start":0,"token_end":-1,"tensor":null,"calibration":null,"bounds_ppm":null,"sae":null})
    }
    fn activate() -> Value {
        json!({"profile_id":"a".repeat(96),"model_id":"b".repeat(96),"expected_generation":3})
    }
    #[cfg(unix)]
    fn venv_fixture(tag: &str) -> PathBuf {
        // A venv's bin/python is a symlink to the base interpreter.
        let d = std::env::temp_dir().join(format!("atlas-venv-{tag}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&d);
        for dir in ["base", "other", "venv/bin"] {
            std::fs::create_dir_all(d.join(dir)).unwrap();
        }
        std::fs::write(d.join("base/python3.12"), b"base interpreter").unwrap();
        std::fs::write(d.join("other/python3.12"), b"other interpreter").unwrap();
        std::fs::write(d.join("venv/pyvenv.cfg"), b"home = base\n").unwrap();
        std::os::unix::fs::symlink(d.join("base/python3.12"), d.join("venv/bin/python")).unwrap();
        d
    }
    #[cfg(unix)]
    #[test]
    fn a_venv_interpreter_runs_from_the_selected_path() {
        // Python finds pyvenv.cfg from the path it was started with, so running the
        // resolved target loses the venv's site-packages.
        let d = venv_fixture("path");
        let (python, target) = interpreter(&d.join("venv/bin/python")).unwrap();
        assert_eq!(python, d.join("venv/bin/python"));
        assert_eq!(target, d.join("base/python3.12").canonicalize().unwrap());
        let _ = std::fs::remove_dir_all(&d);
    }
    #[cfg(unix)]
    #[test]
    fn a_repointed_venv_interpreter_is_refused() {
        let d = venv_fixture("repoint");
        let (python, target) = interpreter(&d.join("venv/bin/python")).unwrap();
        let hash = atlas_core::hash(&files::read_bounded(&target, 1024).unwrap());
        assert!(verify_interpreter(&python, &target, &hash).is_ok());
        std::fs::remove_file(d.join("venv/bin/python")).unwrap();
        std::os::unix::fs::symlink(d.join("other/python3.12"), d.join("venv/bin/python")).unwrap();
        let refused = verify_interpreter(&python, &target, &hash).unwrap_err();
        assert_eq!(refused.code, "LAB_INTERPRETER_CHANGED");
        let _ = std::fs::remove_dir_all(&d);
    }
    #[test]
    fn one_operator_review_is_complete() {
        let ops = json!([operator("op-1", 2, &[4, 9], 1_500_000)]);
        let review = intervention_review("activate", "op", &activate(), Some(&ops)).unwrap();
        assert!(review.len() <= 1200, "{review}");
        assert!(review.contains("\"coefficient_ppm\": 1500000"), "{review}");
    }
    #[test]
    fn compact_review_keeps_signed_operators() {
        let wide: Vec<u64> = (0..40).collect();
        let ops = json!([
            operator("op-1", 0, &[1, 2], 1_500_000),
            operator("op-2", 5, &[7], -250_000),
            operator("op-3", 11, &wide, 2_000_000)
        ]);
        let review = intervention_review("activate", "op", &activate(), Some(&ops)).unwrap();
        assert!(review.contains("the dialog omitted the rest"), "{review}");
        for shown in ["op-1", "op-2", "op-3", "-250000", "layer 11"] {
            assert!(review.contains(shown), "{shown} missing from\n{review}");
        }
        let digest = atlas_core::hash(serde_json::to_string(&ops).unwrap().as_bytes());
        assert!(review.contains(&digest), "{review}");
        assert!(review.lines().count() <= 24, "{review}");
    }
    #[test]
    fn compact_review_bounds_many_operators() {
        let ops = Value::Array(
            (0..64)
                .map(|i| operator(&format!("op-{i}"), i, &[i], 1_000_000))
                .collect(),
        );
        let review = intervention_review("activate", "op", &activate(), Some(&ops)).unwrap();
        assert!(review.contains("Exact signed operators: 64"), "{review}");
        assert!(
            review.contains("op-7") && !review.contains("op-8 "),
            "{review}"
        );
        assert!(review.contains("56 more"), "{review}");
        assert!(review.lines().count() <= 24, "{review}");
    }
}
