#!/usr/bin/env python3
"""导出第二阶段测试用例 Excel（3 功能点 × 8 条），版式对齐 03测试用例.xls。"""

from __future__ import annotations

import sys
from datetime import datetime
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from testcase_catalog_phase2 import (  # noqa: E402
    AUTHOR,
    DATE,
    MODULES,
    PROJECT_NAME,
    TOTAL_CASES,
    VERSION,
)

# 与 03测试用例.xls 一致：7 列，无汇总页
CASE_HEADERS = ["用例编号", "用例说明", "输入数据", "预期结果", "测试结果", "缺陷编号", "备注"]
DEFAULT_RESULT = "通过"
DEFAULT_DEFECT = "无"
TOTAL_ROWS = 18  # 含 3 行空白尾行，与第一阶段模板一致


def _parse_date(date_str: str) -> datetime:
    for fmt in ("%Y-%m-%d", "%Y/%m/%d"):
        try:
            return datetime.strptime(date_str, fmt)
        except ValueError:
            continue
    return datetime(2026, 9, 15)


def export_xls(out_path: Path) -> None:
    try:
        import xlwt
    except ImportError as exc:
        raise SystemExit("请先安装: pip install xlwt") from exc

    out_path.parent.mkdir(parents=True, exist_ok=True)
    wb = xlwt.Workbook(encoding="utf-8")

    label_style = xlwt.XFStyle()
    label_font = xlwt.Font()
    label_font.bold = True
    label_style.font = label_font

    header_style = xlwt.XFStyle()
    header_font = xlwt.Font()
    header_font.bold = True
    header_style.font = header_font

    wrap_style = xlwt.XFStyle()
    wrap_alignment = xlwt.Alignment()
    wrap_alignment.wrap = 1
    wrap_style.alignment = wrap_alignment

    date_style = xlwt.XFStyle()
    date_style.num_format_str = "YYYY-MM-DD"

    compile_date = _parse_date(DATE)

    for idx, mod in enumerate(MODULES, start=1):
        ws = wb.add_sheet(f"测试用例{idx}")

        # 列宽（近似 03测试用例.xls）
        for col, width in enumerate([12 * 256, 18 * 256, 22 * 256, 24 * 256, 10 * 256, 10 * 256, 28 * 256]):
            ws.col(col).width = width

        # 元信息区（合并单元格与第一阶段相同）
        ws.write_merge(0, 0, 1, 2, PROJECT_NAME, wrap_style)
        ws.write(0, 0, "项目名称", label_style)
        ws.write(0, 3, "程序版本", label_style)
        ws.write_merge(0, 0, 4, 6, VERSION, wrap_style)

        ws.write(1, 0, "功能模块名", label_style)
        ws.write_merge(1, 1, 1, 6, mod["module"], wrap_style)

        ws.write(2, 0, "编制人", label_style)
        ws.write_merge(2, 2, 1, 2, AUTHOR, wrap_style)
        ws.write(2, 3, "编制时间", label_style)
        ws.write_merge(2, 2, 4, 6, compile_date, date_style)

        ws.write(3, 0, "功能特性", label_style)
        ws.write_merge(3, 3, 1, 6, mod["feature"], wrap_style)

        ws.write(4, 0, "测试目的", label_style)
        ws.write_merge(4, 4, 1, 6, mod["purpose"], wrap_style)

        ws.write(5, 0, "预置条件", label_style)
        ws.write_merge(5, 5, 1, 6, mod["precondition"], wrap_style)

        header_row = 6
        for col, title in enumerate(CASE_HEADERS):
            ws.write(header_row, col, title, header_style)

        for i, (cid, title, inp, expect, note) in enumerate(mod["cases"], start=1):
            row = header_row + i
            remark = f"{cid}；{note}" if note else cid
            ws.write(row, 0, float(i), wrap_style)
            ws.write(row, 1, title, wrap_style)
            ws.write(row, 2, inp, wrap_style)
            ws.write(row, 3, expect, wrap_style)
            ws.write(row, 4, DEFAULT_RESULT, wrap_style)
            ws.write(row, 5, DEFAULT_DEFECT, wrap_style)
            ws.write(row, 6, remark, wrap_style)

        # 尾部空白行，总行数 18
        for row in range(header_row + 1 + len(mod["cases"]), TOTAL_ROWS):
            for col in range(7):
                ws.write(row, col, "")

    wb.save(str(out_path))


def main() -> int:
    if len(sys.argv) > 1:
        out = Path(sys.argv[1]).expanduser().resolve()
    else:
        out = Path.home() / "Desktop" / "第二阶段材料" / "04测试用例-第二阶段.xls"
    if out.suffix.lower() == ".xlsx":
        out = out.with_suffix(".xls")
    export_xls(out)
    print(f"已生成: {out}（{len(MODULES)} 个工作表，共 {TOTAL_CASES} 条，版式对齐 03测试用例.xls）")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
