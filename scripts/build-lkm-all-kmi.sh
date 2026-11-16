#!/usr/bin/env bash
# 批量构建 MRSU 全 KMI 内置 LKM（kernelsu.ko），产物直接落到
# userspace/ksud/bin/aarch64/<kmi>_kernelsu.ko，供 ksud 的 RustEmbed 内嵌，
# 从而让管理器像官方 KernelSU 一样支持 5.10 及以上全部 GKI 机型。
#
# 依赖：Linux + docker（拉 ghcr.io/ylarod/ddk-min 镜像，单镜像 1~4GB，共约 8 个）
# 用法：
#   bash scripts/build-lkm-all-kmi.sh                # 全 8 档
#   KMIS="android12-5.10" bash scripts/build-lkm-all-kmi.sh   # 只编指定档位
#
# 身份参数与 kernel/Kbuild 默认值保持一致（MRSU = com.mingri.ru）。
set -euo pipefail

DDK_RELEASE="${DDK_RELEASE:-20260828}"
KMIS="${KMIS:-android12-5.10 android13-5.10 android13-5.15 android14-5.15 android14-6.1 android15-6.6 android16-6.12 android17-6.18}"

KSU_PACKAGE_NAME="${KSU_PACKAGE_NAME:-com.mingri.ru}"
KSU_EXPECTED_SIZE="${KSU_EXPECTED_SIZE:-0x036d}"
KSU_EXPECTED_HASH="${KSU_EXPECTED_HASH:-5ccb0ad242b96baea99ebdf96d6ee01875781a3784693d4299319155c9843f2d}"

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="$REPO_ROOT/userspace/ksud/bin/aarch64"
mkdir -p "$OUT_DIR"

echo "== 仓库: $REPO_ROOT"
echo "== 输出: $OUT_DIR"
echo "== KMI : $KMIS"
echo "== 身份: $KSU_PACKAGE_NAME size=$KSU_EXPECTED_SIZE hash=$KSU_EXPECTED_HASH"
echo

FAILED=""
for kmi in $KMIS; do
  img="ghcr.io/ylarod/ddk-min:${kmi}-${DDK_RELEASE}"
  echo "==================== $kmi ===================="
  echo "-- 拉取 $img"
  if ! docker pull "$img"; then
    echo "!! 镜像拉取失败: $img"
    FAILED="$FAILED $kmi(pull)"
    continue
  fi

  # 镜像内已带该 KMI 的 arm64 kdir（/opt/ddk）与 clang 交叉编译环境，
  # 与 .github/workflows/ddk-lkm.yml 的 arm64 分支完全一致。
  if docker run --rm --privileged \
      -v "$REPO_ROOT":/work \
      -w /work/kernel \
      -e KSU_PACKAGE_NAME="$KSU_PACKAGE_NAME" \
      -e KSU_EXPECTED_SIZE="$KSU_EXPECTED_SIZE" \
      -e KSU_EXPECTED_HASH="$KSU_EXPECTED_HASH" \
      "$img" \
      bash -lc 'git config --global --add safe.directory "*" 2>/dev/null || true;
                make clean >/dev/null 2>&1 || true;
                CONFIG_KSU=m CC=clang make \
                  KSU_PACKAGE_NAME="$KSU_PACKAGE_NAME" \
                  KSU_EXPECTED_SIZE="$KSU_EXPECTED_SIZE" \
                  KSU_EXPECTED_HASH="$KSU_EXPECTED_HASH"'; then
    echo "!! 构建失败: $kmi"
    FAILED="$FAILED $kmi(build)"
    continue
  fi

  if [ ! -f "$REPO_ROOT/kernel/kernelsu.ko" ]; then
    echo "!! 未产出 kernelsu.ko: $kmi"
    FAILED="$FAILED $kmi(no-ko)"
    continue
  fi

  cp -f "$REPO_ROOT/kernel/kernelsu.ko" "$OUT_DIR/${kmi}_kernelsu.ko"
  # 与 CI 一致：去掉调试符号，减小内嵌体积
  if command -v llvm-strip >/dev/null 2>&1; then
    llvm-strip -d "$OUT_DIR/${kmi}_kernelsu.ko" || true
  elif docker image inspect "$img" >/dev/null 2>&1; then
    docker run --rm -v "$OUT_DIR":/out "$img" \
      bash -lc "llvm-strip -d /out/${kmi}_kernelsu.ko" || true
  fi
  rm -f "$REPO_ROOT/kernel/kernelsu.ko"

  ko_size=$(wc -c < "$OUT_DIR/${kmi}_kernelsu.ko")
  echo "-- 完成: ${ko_size// /} bytes"
  echo
done

echo "==================== 结果 ===================="
ls -l "$OUT_DIR"/*_kernelsu.ko 2>/dev/null || echo "（无 ko 产出）"
if [ -n "$FAILED" ]; then
  echo "失败档位:$FAILED"
  exit 1
fi
echo "全部成功。下一步：重新编译 ksud，使 RustEmbed 将 ko 内嵌进 libksud.so。"
