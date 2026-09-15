#!/usr/bin/env python3
"""导出第二阶段测试用例 Excel（3 功能点 × 8 条）。"""

from __future__ import annotations

import sys
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

HEADERS = [
    "序号",
    "用例编号",
    "测试项",
    "输入数据/操作步骤",
    "预期结果",
    "实测结果",
    "是否通过",
    "备注",
]


def export_xlsx(out_path: Path) -> None:
    try:
        from openpyxl import Workbook
        from openpyxl.styles import Alignment, Font, PatternFill
    except ImportError as exc:
        raise SystemExit("请先安装: pip install openpyxl") from exc

    out_path.parent.mkdir(parents=True, exist_ok=True)
    wb = Workbook()
    wb.remove(wb.active)

    title_font = Font(bold=True, size=12)
    header_fill = PatternFill("solid", fgColor="D9E1F2")
    header_font = Font(bold=True)
    wrap = Alignment(wrap_text=True, vertical="top")

    for idx, mod in enumerate(MODULES, start=1):
        ws = wb.create_sheet(title=f"测试用例{idx}")
        ws.append([f"项目名称：{PROJECT_NAME}"])
        ws.append([f"版本：{VERSION}    作者：{AUTHOR}    日期：{DATE}"])
        ws.append([f"功能模块：{mod['module']}"])
        ws.append([f"功能描述：{mod['feature']}"])
        ws.append([f"测试目的：{mod['purpose']}"])
        ws.append([f"前置条件：{mod['precondition']}"])
        ws.append([])

        ws.append(HEADERS)
        for col in range(1, len(HEADERS) + 1):
            cell = ws.cell(row=ws.max_row, column=col)
            cell.fill = header_fill
            cell.font = header_font
            cell.alignment = wrap

        for i, (cid, title, inp, expect, note) in enumerate(mod["cases"], start=1):
            ws.append([i, cid, title, inp, expect, "", "", note])

        for row in ws.iter_rows(min_row=1, max_row=ws.max_row, min_col=1, max_col=len(HEADERS)):
            for cell in row:
                cell.alignment = wrap

        ws.column_dimensions["A"].width = 6
        ws.column_dimensions["B"].width = 12
        ws.column_dimensions["C"].width = 22
        ws.column_dimensions["D"].width = 42
        ws.column_dimensions["E"].width = 32
        ws.column_dimensions["F"].width = 14
        ws.column_dimensions["G"].width = 10
        ws.column_dimensions["H"].width = 18

        ws.merge_cells(start_row=1, start_column=1, end_row=1, end_column=len(HEADERS))
        ws["A1"].font = title_font

    summary = wb.create_sheet(title="汇总", index=0)
    summary.append(["第二阶段测试用例汇总"])
    summary.append(["项目名称", PROJECT_NAME])
    summary.append(["版本", VERSION])
    summary.append(["用例总数", TOTAL_CASES])
    summary.append([])
    summary.append(["工作表", "功能模块", "用例数"])
    for idx, mod in enumerate(MODULES, start=1):
        summary.append([f"测试用例{idx}", mod["module"], len(mod["cases"])])
    summary["A1"].font = title_font

    wb.save(out_path)


def main() -> int:
    if len(sys.argv) > 1:
        out = Path(sys.argv[1]).expanduser().resolve()
    else:
        out = Path.home() / "Desktop" / "第二阶段材料" / "04测试用例-第二阶段.xlsx"
    export_xlsx(out)
    print(f"已生成: {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
