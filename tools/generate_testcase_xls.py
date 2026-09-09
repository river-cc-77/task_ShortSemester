#!/usr/bin/env python3
"""按交付模板格式生成 03测试用例.xls（7 模块 × 7 条 = 49 条）。

模板布局（与 Downloads 中 03测试用例.xls 一致）：
  - 每模块独立工作表，表内结构相同
  - 标签列 A，内容区为合并单元格
  - 用例区 8 行（本项填 7 条，第 8 行留空）
  - 无额外「用例汇总」页

用法：
  python3 tools/generate_testcase_xls.py [输出路径]
  python3 tools/generate_testcase_xls.py --passed [输出路径]
  python3 tools/generate_testcase_xls.py --template <模板.xls> [输出路径]
"""

from __future__ import annotations

import sys
from pathlib import Path

import xlwt

sys.path.insert(0, str(Path(__file__).resolve().parent))
from testcase_catalog import AUTHOR, DATE, MODULES, PROJECT_NAME, VERSION

DEFAULT_TEMPLATE = Path(
    r"c:\Users\RiverCG\Downloads\学生成果物-第X组-项目名称\学生成果物-第X组-项目名称"
    r"\第一阶段项目\03.项目文件\03测试用例.xls"
)
DEFAULT_OUTPUT = Path(__file__).resolve().parent.parent / "docs" / "03测试用例.xls"

# 模板列宽（xlrd width 单位）
COL_WIDTHS = [2464, 6048, 3360, 4192, 4384, 2720, 4800]
# 模板行高：元信息行 500，表头/用例行见下
META_ROW_HEIGHT = 500
HEADER_ROW_HEIGHT = 720
CASE_ROW_HEIGHTS = [720, 720, 1002, 1200, 1002, 1200, 1002, 1002]
TAIL_ROW_HEIGHT = 248


def _styles() -> dict[str, xlwt.XFStyle]:
    """尽量贴近模板：宋体、边框、换行。"""
    borders = "borders: left thin, right thin, top thin, bottom thin;"
    label = xlwt.easyxf(
        f"font: name 宋体, height 200; align: vert centre, wrap on; {borders}"
    )
    value = xlwt.easyxf(
        f"font: name 宋体, height 200; align: vert centre, wrap on; {borders}"
    )
    header = xlwt.easyxf(
        f"font: name 宋体, height 200; align: horiz centre, vert centre, wrap on; {borders}"
    )
    case = xlwt.easyxf(
        f"font: name 宋体, height 200; align: vert top, wrap on; {borders}"
    )
    return {"label": label, "value": value, "header": header, "case": case}


def _write_label(sh: xlwt.Worksheet, row: int, text: str, st: dict) -> None:
    sh.write(row, 0, text, st["label"])


def _write_merge(
    sh: xlwt.Worksheet,
    r1: int,
    r2: int,
    c1: int,
    c2: int,
    text: str,
    st: dict,
) -> None:
    sh.write_merge(r1, r2, c1, c2, text, st["value"])


def write_module_sheet(wb: xlwt.Workbook, sheet_idx: int, mod: dict, *, test_result: str = "") -> None:
    """按模板写入一个功能模块页。"""
    # 工作表名：测试用例1 … 测试用例7（与模板主表名一致系列）
    sheet_name = f"测试用例{sheet_idx + 1}"
    sh = wb.add_sheet(sheet_name, cell_overwrite_ok=True)
    st = _styles()

    for c, w in enumerate(COL_WIDTHS):
        sh.col(c).width = w

    # ---- 元信息区（合并单元格与模板一致）----
    _write_label(sh, 0, "项目名称", st)
    sh.write(0, 3, "程序版本", st["label"])
    _write_merge(sh, 0, 0, 1, 2, PROJECT_NAME, st)
    _write_merge(sh, 0, 0, 4, 6, VERSION, st)

    _write_label(sh, 1, "功能模块名", st)
    _write_merge(sh, 1, 1, 1, 6, mod["module"], st)

    _write_label(sh, 2, "编制人", st)
    sh.write(2, 3, "编制时间", st["label"])
    _write_merge(sh, 2, 2, 1, 2, AUTHOR, st)
    _write_merge(sh, 2, 2, 4, 6, DATE, st)

    _write_label(sh, 3, "功能特性", st)
    _write_merge(sh, 3, 3, 1, 6, mod["feature"], st)

    _write_label(sh, 4, "测试目的", st)
    _write_merge(sh, 4, 4, 1, 6, mod["purpose"], st)

    _write_label(sh, 5, "预置条件", st)
    _write_merge(sh, 5, 5, 1, 6, mod["precondition"], st)

    sh.row(0).height = META_ROW_HEIGHT
    sh.row(1).height = META_ROW_HEIGHT
    sh.row(2).height = META_ROW_HEIGHT
    sh.row(3).height = META_ROW_HEIGHT
    sh.row(4).height = META_ROW_HEIGHT
    sh.row(5).height = META_ROW_HEIGHT

    # ---- 用例表头 ----
    headers = ["用例编号", "用例说明", "输入数据", "预期结果", "测试结果", "缺陷编号", "备注"]
    for c, title in enumerate(headers):
        sh.write(6, c, title, st["header"])
    sh.row(6).height = HEADER_ROW_HEIGHT

    # ---- 用例明细：模板 8 行，填 7 条 + 1 空行 ----
    cases = mod["cases"]
    for i in range(8):
        row = 7 + i
        sh.row(row).height = CASE_ROW_HEIGHTS[i]
        sh.write(row, 0, str(i + 1), st["case"])
        if i < len(cases):
            row_data = cases[i]
            case_id, desc, inp, expect = row_data[:4]
            remark = row_data[4] if len(row_data) > 4 else f"自动化 {case_id}"
            sh.write(row, 1, desc, st["case"])
            sh.write(row, 2, inp, st["case"])
            sh.write(row, 3, expect, st["case"])
            sh.write(row, 4, test_result, st["case"])
            sh.write(row, 5, "", st["case"])
            sh.write(row, 6, remark, st["case"])
        else:
            for c in range(1, 7):
                sh.write(row, c, "", st["case"])

    # 模板底部 3 行空白
    for row in (15, 16, 17):
        sh.row(row).height = TAIL_ROW_HEIGHT
        for c in range(7):
            sh.write(row, c, "", st["case"])


def build_workbook(*, mark_passed: bool = False) -> xlwt.Workbook:
    wb = xlwt.Workbook(encoding="utf-8")
    result = "通过" if mark_passed else ""
    for idx, mod in enumerate(MODULES):
        write_module_sheet(wb, idx, mod, test_result=result)
    return wb


def parse_args(argv: list[str]) -> tuple[Path | None, list[Path], bool]:
    template: Path | None = None
    outputs: list[Path] = []
    mark_passed = False
    i = 0
    while i < len(argv):
        if argv[i] == "--template" and i + 1 < len(argv):
            template = Path(argv[i + 1])
            i += 2
        elif argv[i] == "--passed":
            mark_passed = True
            i += 1
        else:
            outputs.append(Path(argv[i]))
            i += 1
    if not outputs:
        outputs = [DEFAULT_OUTPUT]
        if DEFAULT_TEMPLATE.parent.exists():
            outputs.append(DEFAULT_TEMPLATE)
    return template, outputs, mark_passed


def main() -> int:
    template, out_paths, mark_passed = parse_args(sys.argv[1:])
    if template and not template.exists():
        print(f"Warning: template not found: {template}", file=sys.stderr)

    wb = build_workbook(mark_passed=mark_passed)
    for path in out_paths:
        path.parent.mkdir(parents=True, exist_ok=True)
        wb.save(str(path))
        print(f"Wrote {path}")

    print(f"Total cases: {sum(len(m['cases']) for m in MODULES)}")
    print(f"Sheets: {len(MODULES)} × 模板「测试用例」布局")
    if mark_passed:
        print("测试结果列: 通过")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
