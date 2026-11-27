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
    let state = ksucalls::get_feature(ksu_uapi::ksu_feature_id_KSU_FEATURE_KPM as u32)?;
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

/// Load every `.kpm` staged in `defs::KPM_DIR`, in file-name order.
///
/// Called from the post-fs-data stage so staged modules come back on every
/// boot. One failing module must not stop the others: their failures are
/// reported and counted, and the caller only logs the summary.
pub fn autoload() -> Result<()> {
    ensure_supported()?;
    let dir = Path::new(defs::KPM_DIR);
    if !dir.is_dir() {
        return Ok(());
    }

    let mut images: Vec<PathBuf> = fs::read_dir(dir)
        .with_context(|| format!("read {} failed", dir.display()))?
        .flatten()
        .map(|entry| entry.path())
        .filter(|path| {
            path.is_file()
                && path
                    .extension()
                    .is_some_and(|ext| ext.eq_ignore_ascii_case("kpm"))
        })
        .collect();

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
        bail!("{failed} of {} staged kpm module(s) failed", images.len());
    }
    Ok(())
}

/// Copy a `.kpm` into `defs::KPM_DIR` so it is loaded on every boot.
pub fn stage(path: &Path) -> Result<()> {
    let name = path
        .file_name()
        .with_context(|| format!("invalid module path: {}", path.display()))?
        .to_owned();
    if !path.is_file() {
        bail!("not a file: {}", path.display());
    }
    fs::create_dir_all(defs::KPM_DIR)
        .with_context(|| format!("create {} failed", defs::KPM_DIR))?;
    let dest = Path::new(defs::KPM_DIR).join(&name);
    fs::copy(path, &dest).with_context(|| format!("copy to {} failed", dest.display()))?;
    println!("Staged {} for autoload", dest.display());
    Ok(())
}

/// Forget a staged module; the loaded image (if any) stays until unloaded.
pub fn unstage(name: &str) -> Result<()> {
    let file_name = if name.ends_with(".kpm") {
        name.to_owned()
    } else {
        format!("{name}.kpm")
    };
    let path = Path::new(defs::KPM_DIR).join(&file_name);
    if !path.is_file() {
        bail!("no staged module named {file_name}");
    }
    fs::remove_file(&path).with_context(|| format!("remove {} failed", path.display()))?;
    println!("Removed staged module {file_name}");
    Ok(())
}

/// List the staged modules without asking the kernel.
pub fn list_staged() -> Result<()> {
    let dir = Path::new(defs::KPM_DIR);
    if !dir.is_dir() {
        println!("No staged kpm module.");
        return Ok(());
    }
    let mut names: Vec<String> = fs::read_dir(dir)?
        .flatten()
        .filter(|entry| {
            entry.path().is_file()
                && entry
                    .path()
                    .extension()
                    .is_some_and(|ext| ext.eq_ignore_ascii_case("kpm"))
        })
        .map(|entry| entry.file_name().to_string_lossy().into_owned())
        .collect();
    if names.is_empty() {
        println!("No staged kpm module.");
        return Ok(());
    }
    names.sort();
    for name in names {
        println!("{name}");
    }
    Ok(())
}

/// Load the standalone runtime (kpm.ko) that this ksud embeds.
///
/// kpm.ko carries the whole runtime -- ELF loader, hook engine, kfuncs and its
/// own module table -- and takes the KPM control channel over as soon as its
/// init runs. `kpm load|list|...` therefore keep working unchanged; from then
/// on they land in kpm.ko's table (dmesg: "control channel now served by
/// kpm.ko"). This is the deployability path: a KernelSU module can hand out KPM
/// by insmod'ing this ko, without touching the boot image.
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
/// round trip, the staged directory and the recent kernel log.
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
        match ksucalls::get_feature(ksu_uapi::ksu_feature_id_KSU_FEATURE_KPM as u32) {
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

    let dir = Path::new(defs::KPM_DIR);
    if dir.is_dir() {
        let staged = fs::read_dir(dir)
            .map(|entries| {
                entries
                    .flatten()
                    .filter(|entry| {
                        entry
                            .path()
                            .extension()
                            .is_some_and(|ext| ext.eq_ignore_ascii_case("kpm"))
                    })
                    .count()
            })
            .unwrap_or(0);
        println!("staged dir     : {} ({staged} .kpm)", defs::KPM_DIR);
    } else {
        println!("staged dir     : {} (missing)", defs::KPM_DIR);
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
