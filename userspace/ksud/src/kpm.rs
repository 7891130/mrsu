//! Userspace side of the KPM (KernelPatch module) runtime.

use anyhow::{Context, Result, bail};
use std::ffi::CString;
use std::path::Path;

use crate::{ksu_uapi, ksucalls};

const OUT_BUF: usize = 8192;

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
    let text = fetch_text(ksu_uapi::KSU_KPM_OP_LIST, None, None)?;
    if text.trim().is_empty() {
        println!("No kernel patch module loaded.");
        return Ok(());
    }
    print!("{text}");
    Ok(())
}

pub fn info(name: &str) -> Result<()> {
    let text = fetch_text(ksu_uapi::KSU_KPM_OP_INFO, Some(name), None)?;
    print!("{text}");
    Ok(())
}

/// Module-defined control command; the module writes its reply to `out`.
pub fn ctl0(name: &str, args: &str) -> Result<()> {
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
