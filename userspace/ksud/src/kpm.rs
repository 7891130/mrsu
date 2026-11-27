//! Userspace side of the KPM (KernelPatch module) runtime.

use anyhow::{Context, Result, bail};
use rustix::cstr;
use std::ffi::CString;
use std::fs;
use std::path::{Path, PathBuf};

use crate::{defs, ksu_uapi, ksucalls};

const OUT_BUF: usize = 8192;

/// First UAPI version that exposes `KSU_IOCTL_KPM`.
const KPM_MIN_UAPI_VERSION: u32 = 7;

/// Fail early with an actionable message instead of an ENOTTY from the ioctl.
fn ensure_supported() -> Result<()> {
    let info = ksucalls::get_info();
    if info.uapi_version < KPM_MIN_UAPI_VERSION {
        bail!(
            "this kernel build predates the kpm interface (UAPI {} < {}); update the kernel module",
            info.uapi_version,
            KPM_MIN_UAPI_VERSION
        );
    }
    let state = ksucalls::get_feature(ksu_uapi::ksu_feature_id_KSU_FEATURE_KPM)?;
    if !state.supported {
        bail!("this kernel build does not support the kpm feature");
    }
    if state.value == 0 {
        bail!("the kpm feature is disabled; enable it in the manager settings");
    }
    Ok(())
}

fn empty_cmd(op: u32) -> ksu_uapi::ksu_kpm_cmd {
    ksu_uapi::ksu_kpm_cmd {
        op,
        out_len: 0,
        name: 0,
        path: 0,
        args: 0,
        out: 0,
        a1: 0,
        a2: 0,
        a3: 0,
        result: 0,
        reserved: 0,
    }
}

fn result_to_errno(result: u32) -> i64 {
    i64::from(result as i32)
}

/// Load a .kpm image. `event` and `args` follow KernelPatch's semantics: `args`
/// is handed to the module's `.kpm.init`.
pub fn load(path: &Path, args: Option<&str>) -> Result<()> {
    ensure_supported()?;
    let path = path
        .canonicalize()
        .with_context(|| format!("resolve module path failed: {}", path.display()))?;
    let c_path = CString::new(path.to_string_lossy().as_bytes())?;
    let c_args = match args {
        Some(a) if !a.is_empty() => Some(CString::new(a)?),
        _ => None,
    };

    let mut out = vec![0u8; OUT_BUF];
    let mut cmd = empty_cmd(ksu_uapi::KSU_KPM_OP_LOAD);
    cmd.path = c_path.as_ptr() as u64;
    cmd.args = c_args.as_ref().map_or(0, |a| a.as_ptr() as u64);
    cmd.out = out.as_mut_ptr() as u64;
    cmd.out_len = out.len() as u32;

    ksucalls::kpm_ctl(&mut cmd)?;

    let rc = result_to_errno(cmd.result);
    let msg = String::from_utf8_lossy(&out)
        .trim_end_matches('\0')
        .trim()
        .to_string();
    if rc < 0 {
        if msg.is_empty() {
            bail!("load failed: errno {}", -rc);
        }
        bail!("{msg}");
    }

    println!("Loaded kernel patch module: {}", path.display());
    Ok(())
}

/// Unload a module by name; its `.kpm.exit` runs before the image is freed.
pub fn unload(name: &str) -> Result<()> {
    ensure_supported()?;
    let c_name = CString::new(name)?;
    let mut out = vec![0u8; OUT_BUF];
    let mut cmd = empty_cmd(ksu_uapi::KSU_KPM_OP_UNLOAD);
    cmd.name = c_name.as_ptr() as u64;
    cmd.out = out.as_mut_ptr() as u64;
    cmd.out_len = out.len() as u32;

    ksucalls::kpm_ctl(&mut cmd)?;

    let rc = result_to_errno(cmd.result);
    if rc < 0 {
        bail!("unload failed: errno {}", -rc);
    }
    println!("Unloaded kernel patch module: {name}");
    Ok(())
}

fn fetch_text(op: u32, name: Option<&str>, args: Option<&str>) -> Result<String> {
    let c_name = match name {
        Some(n) => Some(CString::new(n)?),
        None => None,
    };
    let c_args = match args {
        Some(a) => Some(CString::new(a)?),
        None => None,
    };

    let mut out = vec![0u8; OUT_BUF];
    let mut cmd = empty_cmd(op);
    cmd.name = c_name.as_ref().map_or(0, |n| n.as_ptr() as u64);
    cmd.args = c_args.as_ref().map_or(0, |a| a.as_ptr() as u64);
    cmd.out = out.as_mut_ptr() as u64;
    cmd.out_len = out.len() as u32;

    ksucalls::kpm_ctl(&mut cmd)?;

    let rc = result_to_errno(cmd.result);
    if rc < 0 {
        let msg = String::from_utf8_lossy(&out).trim_end_matches('\0').trim().to_string();
        if msg.is_empty() {
            bail!("request failed: errno {}", -rc);
        }
        bail!("{msg}");
    }

    Ok(String::from_utf8_lossy(&out).trim_end_matches('\0').to_string())
}

/// `name\tversion\targs` per line, empty when nothing is loaded.
pub fn list() -> Result<()> {
    ensure_supported()?;
    let text = fetch_text(ksu_uapi::KSU_KPM_OP_LIST, None, None)?;
    if text.trim().is_empty() {
        println!("No kernel patch module loaded.");
        return Ok(());
    }
    print!("{text}");
    Ok(())
}

pub fn info(name: &str) -> Result<()> {
    ensure_supported()?;
    let text = fetch_text(ksu_uapi::KSU_KPM_OP_INFO, Some(name), None)?;
    print!("{text}");
    Ok(())
}

/// Module-defined control command; the module writes its reply to `out`.
pub fn ctl0(name: &str, args: &str) -> Result<()> {
    ensure_supported()?;
    let c_name = CString::new(name)?;
    let c_args = CString::new(args)?;
    let mut out = vec![0u8; OUT_BUF];
    let mut cmd = empty_cmd(ksu_uapi::KSU_KPM_OP_CTL0);
    cmd.name = c_name.as_ptr() as u64;
    cmd.args = c_args.as_ptr() as u64;
    cmd.out = out.as_mut_ptr() as u64;
    cmd.out_len = out.len() as u32;

    ksucalls::kpm_ctl(&mut cmd)?;

    let rc = result_to_errno(cmd.result);
    if rc < 0 {
        bail!("ctl0 failed: errno {}", -rc);
    }

    let text = String::from_utf8_lossy(&out).trim_end_matches('\0').to_string();
    if !text.is_empty() {
        print!("{text}");
        if !text.ends_with('\n') {
            println!();
        }
    }
    Ok(())
}

/// Raw three-argument control command, mainly for module-specific tooling.
pub fn ctl1(name: &str, a1: u64, a2: u64, a3: u64) -> Result<i64> {
    ensure_supported()?;
    let c_name = CString::new(name)?;
    let mut cmd = empty_cmd(ksu_uapi::KSU_KPM_OP_CTL1);
    cmd.name = c_name.as_ptr() as u64;
    cmd.a1 = a1;
    cmd.a2 = a2;
    cmd.a3 = a3;

    ksucalls::kpm_ctl(&mut cmd)?;

    let rc = result_to_errno(cmd.result);
    if rc < 0 {
        bail!("ctl1 failed: errno {}", -rc);
    }
    println!("ctl1 returned {rc}");
    Ok(rc)
}

/// Kernel-side facts about one loaded module, from `kpm info`.
struct KernelModuleInfo {
    name: String,
    version: String,
    author: String,
    description: String,
}

/// One entry of the on-disk store: `<KPM_DIR>/<id>/<id>.kpm`.
struct InstalledEntry {
    id: String,
    file: PathBuf,
    disabled: bool,
}

fn store_dir() -> &'static Path {
    Path::new(defs::KPM_DIR)
}

fn module_dir(id: &str) -> PathBuf {
    store_dir().join(id)
}

fn module_file(id: &str) -> PathBuf {
    module_dir(id).join(format!("{id}.kpm"))
}

fn disable_marker(id: &str) -> PathBuf {
    module_dir(id).join("disable")
}

/// A store id has to be a single, safe path component.
fn check_id(id: &str) -> Result<()> {
    if id.is_empty() || id == "." || id == ".." || id.contains('/') || id.contains('\0') {
        bail!("invalid module id: {id}");
    }
    Ok(())
}

fn is_kpm(path: &Path) -> bool {
    path.is_file() && path.extension().is_some_and(|ext| ext.eq_ignore_ascii_case("kpm"))
}

/// Every installed module, sorted by id.
fn installed_entries(dir: &Path) -> Result<Vec<InstalledEntry>> {
    let mut entries = Vec::new();

    let read = match fs::read_dir(dir) {
        Ok(read) => read,
        Err(_) => return Ok(entries),
    };
    for entry in read.flatten() {
        let path = entry.path();

        // The store layout: <dir>/<id>/<id>.kpm .
        if path.is_dir() {
            let id = entry.file_name().to_string_lossy().into_owned();
            let file = path.join(format!("{id}.kpm"));
            if !is_kpm(&file) {
                continue;
            }
            entries.push(InstalledEntry {
                disabled: path.join("disable").exists(),
                id,
                file,
            });
            continue;
        }

        // Older layout: a .kpm dropped straight into the directory. Those used
        // to be invisible here, which made them look like a module that cannot
        // be removed -- they only showed up as "loaded" from the kernel.
        if is_kpm(&path) {
            let Some(id) = path.file_stem().map(|stem| stem.to_string_lossy().into_owned()) else {
                continue;
            };
            entries.push(InstalledEntry {
                disabled: false,
                id,
                file: path,
            });
        }
    }
    entries.sort_by(|a, b| a.id.cmp(&b.id));
    Ok(entries)
}

/// Read the module metadata straight out of the ELF's `.kpm.info` section.
///
/// The kernel parses the same section when it loads the module, but `install`
/// has to know the name *before* anything is loaded: the id decides the
/// directory name, and a name clash must be refused up front.
fn read_kpm_info(path: &Path) -> Result<KernelModuleInfo> {
    let data = fs::read(path).with_context(|| format!("read {} failed", path.display()))?;
    let section = elf_section(&data, ".kpm.info")
        .with_context(|| format!("{} is not a .kpm with a .kpm.info section", path.display()))?;

    let mut info = KernelModuleInfo {
        name: String::new(),
        version: String::new(),
        author: String::new(),
        description: String::new(),
    };

    // Entries are NUL terminated `key=value` strings; APatch's kptools also
    // prints an `[kpm]` header around them, so tolerate newlines and that line.
    for field in section.split(|byte| *byte == 0 || *byte == b'\n') {
        let text = String::from_utf8_lossy(field);
        let text = text.trim();
        if text.is_empty() || text.starts_with('[') {
            continue;
        }
        let Some((key, value)) = text.split_once('=') else {
            continue;
        };
        match key.trim() {
            "name" => info.name = value.trim().to_string(),
            "version" => info.version = value.trim().to_string(),
            "author" => info.author = value.trim().to_string(),
            "description" => info.description = value.trim().to_string(),
            _ => {}
        }
    }

    if info.name.is_empty() {
        bail!("{} has no name in .kpm.info", path.display());
    }
    Ok(info)
}

/// Find a section by name in an ELF64 image (little endian, as aarch64 is).
fn elf_section<'a>(data: &'a [u8], want: &str) -> Result<&'a [u8]> {
    const EHDR_SIZE: usize = 64;
    const SHDR_SIZE: usize = 64;

    let u16_at = |off: usize| -> Option<u16> {
        data.get(off..off + 2).map(|b| u16::from_le_bytes([b[0], b[1]]))
    };
    let u32_at = |off: usize| -> Option<u32> {
        data.get(off..off + 4)
            .map(|b| u32::from_le_bytes([b[0], b[1], b[2], b[3]]))
    };
    let u64_at = |off: usize| -> Option<u64> {
        data.get(off..off + 8).map(|b| {
            let mut raw = [0u8; 8];
            raw.copy_from_slice(b);
            u64::from_le_bytes(raw)
        })
    };

    if data.len() < EHDR_SIZE || !data.starts_with(b"\x7fELF") || data[4] != 2 {
        bail!("not an ELF64 image");
    }
    if u16_at(16) != Some(1) {
        bail!("not a relocatable object (ET_REL)");
    }

    let shoff = u64_at(40).unwrap_or(0) as usize;
    let shentsize = u16_at(58).unwrap_or(0) as usize;
    let shnum = u16_at(60).unwrap_or(0) as usize;
    let shstrndx = u16_at(62).unwrap_or(0) as usize;
    if shentsize != SHDR_SIZE || shnum == 0 || shstrndx >= shnum {
        bail!("malformed section table");
    }

    let shdr = |idx: usize| -> Option<(u32, u64, u64)> {
        let base = shoff.checked_add(idx.checked_mul(SHDR_SIZE)?)?;
        Some((u32_at(base)?, u64_at(base + 24)?, u64_at(base + 32)?))
    };

    let (_, str_off, str_size) = shdr(shstrndx).ok_or_else(|| anyhow::anyhow!("no section name table"))?;
    let strtab = data
        .get(str_off as usize..(str_off + str_size) as usize)
        .ok_or_else(|| anyhow::anyhow!("section name table is out of bounds"))?;

    for idx in 1..shnum {
        let Some((name_off, offset, size)) = shdr(idx) else {
            break;
        };
        let name_start = strtab.get(name_off as usize..).unwrap_or(&[]);
        let name_end = name_start.iter().position(|b| *b == 0).unwrap_or(0);
        let name = &name_start[..name_end];
        if name == want.as_bytes() {
            return data
                .get(offset as usize..(offset + size) as usize)
                .ok_or_else(|| anyhow::anyhow!("section {want} is out of bounds"));
        }
    }
    bail!("no {want} section")
}

/// Turn a module name into a store id: one safe path component.
///
/// APatch derives its directory from the module name the same way; anything that
/// is not `[A-Za-z0-9._-]` becomes `_`, and the result is capped at 64 bytes.
fn sanitize_id(name: &str) -> String {
    let mut id: String = name
        .chars()
        .map(|c| {
            if c.is_ascii_alphanumeric() || matches!(c, '.' | '_' | '-') {
                c
            } else {
                '_'
            }
        })
        .collect();

    id.truncate(64);
    while id.starts_with('.') {
        id.remove(0);
    }
    if id.is_empty() {
        id.push_str("kpm");
    }
    id
}

/// Install a `.kpm` into the store, and load it now unless asked not to.
///
/// The layout is APatch's: `<KPM_DIR>/<id>/<id>.kpm`, with `<id>` derived from
/// the module's own name. Re-installing the same name replaces the file, which
/// is how an update works -- a copy that is already loaded keeps running until
/// it is unloaded, because the kernel image is a private copy of the file.
pub fn install(path: &Path, load_now: bool) -> Result<()> {
    ensure_supported()?;
    if !path.is_file() {
        bail!("not a file: {}", path.display());
    }

    let info = read_kpm_info(path)?;
    let id = sanitize_id(&info.name);
    check_id(&id)?;

    let dir = module_dir(&id);
    fs::create_dir_all(&dir).with_context(|| format!("create {} failed", dir.display()))?;
    let dest = module_file(&id);
    fs::copy(path, &dest).with_context(|| format!("copy to {} failed", dest.display()))?;

    println!("Installed {} {} -> {}", info.name, info.version, dest.display());

    if load_now {
        load(&dest, None)?;
    } else {
        println!(
            "It loads at boot; `ksud kpm load {}` loads it now.",
            dest.display()
        );
    }
    Ok(())
}

/// Remove an installed module, unloading it first so the image does not outlive
/// its file. A `.kpm` placed directly in the directory (the old layout) works too.
pub fn uninstall(id: &str) -> Result<()> {
    check_id(id)?;

    let dir = module_dir(id);
    if dir.is_dir() {
        // Unload by the name inside the image: the store id is sanitized, the
        // kernel only knows the module's own name.
        if let Ok(info) = read_kpm_info(&module_file(id)) {
            let _ = unload(&info.name);
        }
        fs::remove_dir_all(&dir).with_context(|| format!("remove {} failed", dir.display()))?;
        println!("Removed {id}");
        return Ok(());
    }

    let file = store_dir().join(format!("{id}.kpm"));
    if is_kpm(&file) {
        if let Ok(info) = read_kpm_info(&file) {
            let _ = unload(&info.name);
        }
        fs::remove_file(&file).with_context(|| format!("remove {} failed", file.display()))?;
        println!("Removed staged module {id}");
        return Ok(());
    }

    bail!("no module named {id}");
}

/// Enable or disable a module's boot load (the `disable` marker, as in APatch).
pub fn set_enabled(id: &str, enabled: bool) -> Result<()> {
    check_id(id)?;
    if !module_file(id).is_file() {
        bail!("no installed module named {id}");
    }

    let marker = disable_marker(id);
    if enabled {
        if marker.exists() {
            fs::remove_file(&marker)
                .with_context(|| format!("remove {} failed", marker.display()))?;
        }
        println!("{id} will load at boot");
    } else {
        fs::write(&marker, b"").with_context(|| format!("create {} failed", marker.display()))?;
        println!("{id} will not load at boot");
    }
    Ok(())
}

/// Print the installed modules, one tab-separated line each, for the manager:
///
/// `id \t name \t version \t author \t disabled \t loaded \t description`
///
/// Only `loaded` is authoritative about what runs right now; it comes from the
/// kernel, while `disabled` is the marker file on disk.
pub fn list_installed() -> Result<()> {
    let entries = installed_entries(store_dir())?;
    if entries.is_empty() {
        println!("No installed kpm module.");
        return Ok(());
    }

    let loaded: Vec<String> = match fetch_text(ksu_uapi::KSU_KPM_OP_LIST, None, None) {
        Ok(text) => text
            .lines()
            .filter_map(|line| line.split('\t').next().map(|name| name.trim().to_string()))
            .filter(|name| !name.is_empty())
            .collect(),
        Err(_) => Vec::new(),
    };

    for entry in entries {
        let (name, version, author, description) = match read_kpm_info(&entry.file) {
            Ok(info) => (info.name, info.version, info.author, info.description),
            Err(_) => (entry.id.clone(), String::new(), String::new(), String::new()),
        };
        let is_loaded = loaded.iter().any(|l| *l == name);
        println!(
            "{}\t{}\t{}\t{}\t{}\t{}\t{}",
            entry.id,
            name,
            version,
            author,
            u8::from(entry.disabled),
            u8::from(is_loaded),
            description.replace('\t', " ").replace('\n', " ").replace('\r', " "),
        );
    }
    Ok(())
}
/// Load every installed, enabled module, in name order.
///
/// Called from the post-fs-data stage so installed modules come back on every
/// boot. One failing module must not stop the others: their failures are
/// reported and counted, and the caller only logs the summary.
///
/// The store is APatch's shape: `<KPM_DIR>/<id>/<id>.kpm`, and an empty
/// `<id>/disable` file keeps a module installed but unloaded. Loose `*.kpm`
/// files in the directory itself are still loaded, so layouts written by older
/// builds keep working.
pub fn autoload() -> Result<()> {
    ensure_supported()?;
    let dir = Path::new(defs::KPM_DIR);
    if !dir.is_dir() {
        return Ok(());
    }

    let mut images: Vec<PathBuf> = installed_entries(dir)?
        .into_iter()
        .filter(|entry| !entry.disabled)
        .map(|entry| entry.file)
        .collect();

    // Legacy layout: a .kpm dropped directly into the directory.
    for entry in fs::read_dir(dir)
        .with_context(|| format!("read {} failed", dir.display()))?
        .flatten()
    {
        let path = entry.path();
        if path.is_file() && path.extension().is_some_and(|ext| ext.eq_ignore_ascii_case("kpm")) {
            images.push(path);
        }
    }

    if images.is_empty() {
        return Ok(());
    }
    images.sort();

    let mut failed = 0usize;
    for image in &images {
        match load(image, None) {
            Ok(()) => {}
            Err(e) => {
                failed += 1;
                println!("Failed to load {}: {e:#}", image.display());
            }
        }
    }

    if failed > 0 {
        bail!("{failed} of {} installed kpm module(s) failed", images.len());
    }
    Ok(())
}

/// Load the standalone runtime (kpm.ko) that this ksud embeds.
///
/// kpm.ko carries the whole runtime -- ELF loader, hook engine, kfuncs and its
/// own module table -- and takes the KPM control channel over as soon as its
/// init runs. `kpm load|list|...` therefore keep working unchanged; from then
/// on they land in kpm.ko's table. This is the deployability path: a KernelSU
/// module can hand out KPM by insmod'ing this ko, without touching the boot
/// image.
pub fn insmod(kmi: Option<String>) -> Result<()> {
    ensure_supported()?;

    if Path::new("/sys/module/kpm").exists() {
        bail!("kpm.ko is already loaded; `rmmod kpm` unloads it together with its modules");
    }

    let kmi = match kmi {
        Some(kmi) => kmi,
        None => crate::boot_patch::get_current_kmi().context("failed to detect the current KMI")?,
    };

    let ko_name = format!("{kmi}_kpm.ko");
    let data = crate::assets::get_asset_data(&ko_name).with_context(|| {
        format!("this ksud does not embed {ko_name}; the standalone runtime only ships for aarch64")
    })?;

    ksuinit::load_module(&data, cstr!("")).with_context(|| format!("failed to load {ko_name}"))?;

    println!("{ko_name} loaded");
    println!("control channel now served by kpm.ko");
    Ok(())
}

/// Print everything needed to triage the runtime on a fresh device.
///
/// The kernel side cannot be debugged interactively, so this gathers the
/// observable state in one go: interface presence, feature gate, an ioctl
/// round trip, the store contents and the recent kernel log.
pub fn doctor() -> Result<()> {
    let info = ksucalls::get_info();
    println!("kernel version : {}", info.version);
    println!("uapi version   : {}", info.uapi_version);

    let interface_present = info.uapi_version >= KPM_MIN_UAPI_VERSION;
    println!(
        "kpm interface  : {}",
        if interface_present {
            "present"
        } else {
            "missing (needs uapi >= 7)"
        }
    );

    if interface_present {
        match ksucalls::get_feature(ksu_uapi::ksu_feature_id_KSU_FEATURE_KPM) {
            Ok(state) => println!(
                "kpm feature    : supported={} enabled={} forced={}",
                state.supported,
                state.value != 0,
                state.forced
            ),
            Err(e) => println!("kpm feature    : query failed: {e:#}"),
        }

        match fetch_text(ksu_uapi::KSU_KPM_OP_LIST, None, None) {
            Ok(text) => {
                let loaded = text.lines().filter(|line| !line.trim().is_empty()).count();
                println!("ioctl roundtrip: ok ({loaded} module(s) loaded)");
            }
            Err(e) => println!("ioctl roundtrip: failed: {e:#}"),
        }
    }

    match installed_entries(Path::new(defs::KPM_DIR)) {
        Ok(entries) => println!(
            "store          : {} ({} installed, {} disabled)",
            defs::KPM_DIR,
            entries.len(),
            entries.iter().filter(|entry| entry.disabled).count()
        ),
        Err(e) => println!("store          : {} unreadable: {e:#}", defs::KPM_DIR),
    }

    println!(
        "insmod mode    : {}",
        if info.uapi_version >= 6 {
            "symbol lookup available"
        } else {
            "kallsyms fallback only"
        }
    );
    // 独立运行时是否接管了控制通道：kpm.ko 是独立内核模块，加载后 /sys/module/kpm 存在。
    let standalone_loaded = Path::new("/sys/module/kpm").exists();
    let embedded = crate::boot_patch::get_current_kmi()
        .is_ok_and(|kmi| crate::assets::get_asset_data(&format!("{kmi}_kpm.ko")).is_ok());
    println!(
        "standalone     : {}",
        match (standalone_loaded, embedded) {
            (true, _) => "loaded (kpm.ko owns the control channel)",
            (false, true) => "not loaded; bundled ko available (`ksud kpm insmod`)",
            (false, false) => "not loaded; this ksud bundles no kpm.ko for the current KMI",
        }
    );

    match std::process::Command::new("dmesg").output() {
        Ok(output) => {
            let text = String::from_utf8_lossy(&output.stdout);
            let mut lines: Vec<&str> = text
                .lines()
                .filter(|line| line.contains("kpm") || line.contains("KernelSU"))
                .collect();
            let start = lines.len().saturating_sub(8);
            lines = lines.split_off(start);
            if lines.is_empty() {
                println!("kernel log     : no kpm/KernelSU lines yet");
            } else {
                println!("kernel log     :");
                for line in lines {
                    println!("  {line}");
                }
            }
        }
        Err(_) => println!("kernel log     : cannot read dmesg; run `dmesg | grep -i kpm` manually"),
    }

    Ok(())
}
