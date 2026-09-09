"""24 条集成测试用例（3 功能点 × 8 条），与 test_integration.py / 03测试用例.xls 对齐。

cases 每项：(编号, 说明, 输入, 预期, 备注)
"""

from __future__ import annotations

PROJECT_NAME = "东软电动汽车充电桩应用管理平台"
VERSION = "V1.0"
AUTHOR = "项目组"
DATE = "2026-09-09"

MODULES = [
    {
        "module": "用户登录认证",
        "feature": "用户手机号免密登录/自动注册；手机号格式校验；冻结账号拦截；无效 token 鉴权。",
        "purpose": "黑盒验证登录与鉴权边界（正确/异常/违规输入）。",
        "precondition": "charge-server 已启动(9000)；已执行 schema.sql + seed.sql。",
        "cases": [
            ("TC-01", "正确手机号登录", "phone=13800138001", "ok=true，返回 token、balance", "seed 正常用户 8001"),
            ("TC-02", "新手机号自动注册", "phone=139xxxxxxxx（未注册）", "ok=true，自动创建用户并返回 token", "随机 11 位手机号"),
            ("TC-03", "冻结用户登录", "phone=13800138006", "ok=false，USER_FROZEN", "seed 冻结账号"),
            ("TC-04", "空手机号", "phone=空字符串", "ok=false，INVALID_PARAM", "请输入11位手机号"),
            ("TC-05", "手机号过短", "phone=1380013800（10位）", "ok=false，INVALID_PARAM", "格式校验"),
            ("TC-06", "手机号过长", "phone=138001380011（12位）", "ok=false，INVALID_PARAM", "格式校验"),
            ("TC-07", "含非数字字符", "phone=1380013800a", "ok=false，INVALID_PARAM", "非法字符"),
            ("TC-08", "无效 token 访问", "token=bad-token，station.list", "ok=false，UNAUTHORIZED", "鉴权拦截"),
        ],
    },
    {
        "module": "充电订单全流程",
        "feature": "预约、取消预约、开始充电、进度查询、停止、结算及故障桩拦截。",
        "purpose": "黑盒验证 charge.* 主流程与关键状态转换。",
        "precondition": "用户 8001 已登录且无未完成订单；存在至少 1 根闲置电桩。",
        "cases": [
            ("TC-09", "无未完成订单", "order.check_open", "has_open=false", "主流程前置"),
            ("TC-10", "预约闲置桩", "charge.reserve + 闲置 pile_no", "ok=true，status=预约", "动态选桩"),
            ("TC-11", "取消预约", "charge.cancel 上述 order_no", "ok=true，check_open=false", "用户主动取消"),
            ("TC-12", "开始充电", "再次 reserve + charge.start", "status=充电中", "预约→充电中"),
            ("TC-13", "查询充电进度", "charge.progress", "含 kwh/amount/estimated_remain_seconds>0", "进度轮询"),
            ("TC-14", "停止充电", "charge.stop", "status=待支付，返回 amount", "充电中→待支付"),
            ("TC-15", "用户结算", "charge.settle", "status=已完成，扣减余额", "待支付→已完成"),
            ("TC-16", "故障桩不可预约", "charge.reserve 故障桩", "ok=false，PILE_FAULT", "边界拦截"),
        ],
    },
    {
        "module": "订单管理与待支付",
        "feature": "待支付检查、未完成单拦截、越权结算、余额不足、充值结算、管理端代结算与列表筛选。",
        "purpose": "黑盒验证待支付闭环与管理端订单处理能力。",
        "precondition": "seed 用户 8002 有待支付订单；8004 余额较低；管理员 admin 已登录。",
        "cases": [
            ("TC-17", "待支付订单检查", "8002 order.check_open", "has_open=true，status=待支付", "seed 待支付演示账号"),
            ("TC-18", "有待支付禁止新预约", "8002 charge.reserve", "ok=false，ORDER_EXISTS", "一单未完成规则"),
            ("TC-19", "越权结算他人订单", "8001 settle 8002 的 order_no", "ok=false，FORBIDDEN", "权限隔离"),
            ("TC-20", "余额不足结算", "8004 充电超额后 charge.settle", "ok=false，BALANCE_NOT_ENOUGH", "低余额用户"),
            ("TC-21", "充值后自行结算", "user.recharge + charge.settle", "ok=true，订单已完成", "补足余额后结算"),
            ("TC-22", "管理员代结算", "order.admin.settle", "ok=true，订单已完成", "管理端代结算"),
            ("TC-23", "订单列表筛选", "order.list status=待支付", "返回项 status 均为待支付", "管理端筛选"),
            ("TC-24", "有待支付禁止冻结", "user.freeze user_id=2", "ok=false，有未完成单", "业务规则保护"),
        ],
    },
]

TOTAL_CASES = sum(len(m["cases"]) for m in MODULES)
