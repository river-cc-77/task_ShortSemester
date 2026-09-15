#!/usr/bin/env bash
# 打包第二阶段答辩/提交用数据集（不含 charge.db 全库，仅分析层镜像 + ML 产出）
#
# 用法:
#   bash ml/package_dataset.sh
#   bash ml/package_dataset.sh --upload   # 额外上传到 HDFS（需 HDFS_URI）
#
# 产出: ml/delivery/phase2_dataset_YYYYMMDD.zip

set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

STAMP="$(date +%Y%m%d)"
OUT_DIR="ml/delivery/phase2_dataset_${STAMP}"
ZIP="ml/delivery/phase2_dataset_${STAMP}.zip"

echo ">>> 检查前置数据..."
python3 ml/verify.py
test -d ml/data/hdfs/charging || { echo "缺少 ml/data/hdfs，请先: python3 ml/export_to_hdfs.py"; exit 1; }

echo ">>> 组装目录 $OUT_DIR"
rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR/hdfs" "$OUT_DIR/output" "$OUT_DIR/docs"

cp -r ml/data/hdfs/charging "$OUT_DIR/hdfs/"
cp -f ml/output/evaluation.json "$OUT_DIR/output/" 2>/dev/null || true
cp -f ml/output/load_forecast.csv "$OUT_DIR/output/" 2>/dev/null || true
cp -f ml/output/time_forecast.csv "$OUT_DIR/output/" 2>/dev/null || true
if [ -d ml/output/analytics ]; then
  cp -r ml/output/analytics "$OUT_DIR/output/"
fi

cat > "$OUT_DIR/README.txt" <<EOF
东软充电桩 — 第二阶段提交数据集 (${STAMP})

目录说明:
  hdfs/charging/     HDFS 本地镜像（dws + dim 四层 CSV）
  output/            ML 预测 CSV + evaluation.json + PySpark 分析维度
  docs/              测试用例说明

再生步骤:
  bash ml/run_pipeline.sh --generate 3000
  python3 ml/export_to_hdfs.py --clean --upload   # 答辩机上传 HDFS

校验:
  python3 tools/test_integration_phase2.py --with-dashboard --with-hdfs
EOF

cp docs/phase2.md "$OUT_DIR/docs/" 2>/dev/null || true
cp tools/testcase_catalog_phase2.py "$OUT_DIR/docs/"

mkdir -p ml/delivery
rm -f "$ZIP"
(cd ml/delivery && zip -rq "$(basename "$ZIP")" "$(basename "$OUT_DIR")")

echo ">>> 完成: $ZIP"
ls -lh "$ZIP"

if [[ "${1:-}" == "--upload" ]]; then
  export HDFS_URI="${HDFS_URI:-$(hdfs getconf -confKey fs.defaultFS 2>/dev/null || true)}"
  if [[ -z "${HDFS_URI}" ]]; then
    echo "[!] 未设置 HDFS_URI，跳过上传"
    exit 0
  fi
  echo ">>> 上传到 ${HDFS_URI}/charging/submission/${STAMP}/"
  hdfs dfs -mkdir -p "/charging/submission/${STAMP}"
  hdfs dfs -put -f "$OUT_DIR/hdfs/charging" "/charging/submission/${STAMP}/"
  echo ">>> HDFS 提交路径: /charging/submission/${STAMP}/charging/"
fi
