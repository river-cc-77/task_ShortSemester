"""49 条集成测试用例目录（7 模块 × 7 条），与 test_integration.py / 03测试用例.xls 对齐。"""

from __future__ import annotations

PROJECT_NAME = "东软电动汽车充电桩应用管理平台"
VERSION = "V1.0"
AUTHOR = "项目组"
DATE = "2026-09-09"

# 每个模块：功能模块名、功能特性、测试目的、预置条件、cases[]
MODULES = [
    {
        "module": "连通与登录认证",
        "feature": "TCP 帧协议 ping；用户手机号免密登录/注册；管理员账号密码登录；冻结账号拦截。",
        "purpose": "验证 P0 基础连通与双端鉴权，确保非法登录被正确拒绝。",
        "precondition": "charge-server 已启动(9000)；已执行 schema.sql + seed.sql。",
        "cases": [
            ("TC-01", "ping 连通测试", "cmd=ping，无 token", "ok=true，服务可达"),
            ("TC-02", "正常用户登录", "phone=13800138001", "返回 token、user_id、balance"),
            ("TC-03", "管理员登录", "username=admin, password=123456", "返回 admin token"),
            ("TC-04", "管理员错误密码", "password=wrong", "ok=false，UNAUTHORIZED"),
            ("TC-05", "冻结用户登录", "phone=13800138006", "ok=false，USER_FROZEN"),
            ("TC-06", "新手机号自动注册", "phone=13900001234(未注册)", "ok=true，自动创建用户"),
            ("TC-07", "无效 token 访问", "token=invalid，调用 station.list", "ok=false，UNAUTHORIZED"),
        ],
    },
    {
        "module": "充电站查询与收藏",
        "feature": "附近站点距离排序、关键词筛选、站点详情含电桩；收藏增删查；负荷预测接口。",
        "purpose": "验证用户端找桩核心能力与 P2 收藏/预测读接口。",
        "precondition": "用户 8001 已登录；seed 含 5 个充电站。",
        "cases": [
            ("TC-08", "附近站点列表", "lat/lng=深圳，keyword 空", "items 按 distance_km 升序"),
            ("TC-09", "站名关键词筛选", "keyword=市民中心", "结果包含匹配站名"),
            ("TC-10", "站点详情", "station_id=有效 ID", "返回站点信息与 piles 数组"),
            ("TC-11", "无效站点 ID", "station_id=99999", "ok=false，NOT_FOUND"),
            ("TC-12", "收藏电站", "favorite.add + list + remove", "列表先增后删成功"),
            ("TC-13", "重复收藏", "同一 station_id 连续 add 两次", "第二次仍 ok（幂等）"),
            ("TC-14", "负荷预测列表", "forecast.list horizon=1h", "返回 items 含 predicted_idle_piles"),
        ],
    },
    {
        "module": "充电订单全流程",
        "feature": "预约、取消预约、开始、进度查询(含预估剩余时间)、停止、结算完整状态机。",
        "purpose": "验证 charge.* 主流程与 charge.cancel、estimated_remain_seconds。",
        "precondition": "8001 无未完成订单；存在至少 1 根闲置电桩。",
        "cases": [
            ("TC-15", "检查无未完成单", "8001 order.check_open", "has_open=false"),
            ("TC-16", "预约闲置桩", "charge.reserve pile_no=闲置桩", "返回 order_no，status=预约"),
            ("TC-17", "取消预约", "charge.cancel 上述 order_no", "成功，check_open=false"),
            ("TC-18", "开始充电", "reserve 后 charge.start", "status=充电中，桩→在用"),
            ("TC-19", "查询充电进度", "charge.progress", "含 kwh/amount/estimated_remain_seconds>0"),
            ("TC-20", "停止充电", "charge.stop", "status=待支付，返回 amount"),
            ("TC-21", "结算订单", "charge.settle", "status=已完成，扣减余额"),
        ],
    },
    {
        "module": "订单业务规则与边界",
        "feature": "待支付拦截、故障/占用桩预约、越权结算、非法状态转换、角色隔离。",
        "purpose": "验证协议拦截规则与错误码一致性。",
        "precondition": "8002 有待支付订单；SZ001-03 为故障桩；8003 可登录。",
        "cases": [
            ("TC-22", "8002 待支付检查", "order.check_open token=8002", "has_open=true，status=待支付"),
            ("TC-23", "有待支付禁止新预约", "8002 charge.reserve", "ORDER_EXISTS"),
            ("TC-24", "故障桩不可预约", "8001 reserve SZ001-03", "PILE_FAULT"),
            ("TC-25", "占用桩不可预约", "8003 占桩后 8001 再预约同桩", "PILE_BUSY"),
            ("TC-26", "越权结算他人订单", "8001 settle 8002 订单", "FORBIDDEN"),
            ("TC-27", "预约态禁止 stop", "预约订单 charge.stop", "INVALID_PARAM"),
            ("TC-28", "用户调管理端统计", "8001 stats.overview", "FORBIDDEN"),
        ],
    },
    {
        "module": "用户资料与公告",
        "feature": "资料查询/修改、余额充值；用户端公告列表；管理端公告 CRUD。",
        "purpose": "验证用户中心与公告管理链路。",
        "precondition": "8001 已登录；管理员已登录。",
        "cases": [
            ("TC-29", "获取用户资料", "user.profile.get", "含 balance 字段"),
            ("TC-30", "修改昵称", "user.profile.update nickname", "ok=true"),
            ("TC-31", "余额充值", "user.recharge amount=50", "余额增加"),
            ("TC-32", "非法充值金额", "amount=0 或负数", "ok=false"),
            ("TC-33", "用户端公告列表", "announcement.list", "返回 items 数组"),
            ("TC-34", "管理端创建公告", "announcement.create", "admin.list 可见"),
            ("TC-35", "管理端删除公告", "announcement.delete", "用户端列表恢复"),
        ],
    },
    {
        "module": "管理端统计与操作日志",
        "feature": "KPI 总览(含 pile_status/health)；用户/电桩/订单列表；操作日志筛选。",
        "purpose": "验证管理端只读接口与日志审计筛选能力。",
        "precondition": "管理员 admin 已登录；seed 有历史订单与日志。",
        "cases": [
            ("TC-36", "数据总览 KPI", "stats.overview days=7", "含 today_revenue/pile_status/pile_health_rate"),
            ("TC-37", "用户列表", "user.admin.list", "含 phone/balance/status"),
            ("TC-38", "用户手机号搜索", "phone_keyword=8001", "结果匹配"),
            ("TC-39", "电桩列表筛选", "pile.list status=闲置", "均为闲置桩"),
            ("TC-40", "订单列表筛选", "order.list status=已完成", "状态均为已完成"),
            ("TC-41", "操作日志按动作", "operation_log.list action=登录", "action 均为登录"),
            ("TC-42", "操作日志关键字", "keyword=admin 或 pile_no", "模糊匹配成功"),
        ],
    },
    {
        "module": "管理端写操作与电桩管理",
        "feature": "建站、电桩 CRUD/重启、用户冻结解冻、代结算、pile.detail。",
        "purpose": "验证管理端写操作、事务一致性与 operation_log 写入。",
        "precondition": "管理员已登录；存在至少 1 根故障或可置故障的电桩；8003 无未完成单。",
        "cases": [
            ("TC-43", "新建充电站", "station.create 随机站名", "返回 station_id，列表可见"),
            ("TC-44", "远程重启故障桩", "pile.restart 故障桩 pile_no", "桩 status→闲置，写日志"),
            ("TC-45", "冻结用户 8003", "user.freeze user_id=3", "8003 登录失败"),
            ("TC-46", "解冻用户 8003", "user.freeze freeze=false", "8003 可再登录"),
            ("TC-47", "有待支付禁止冻结", "冻结 8002", "ok=false（有未完成单）"),
            ("TC-48", "管理员代结算", "order.admin.settle 待支付单", "订单已完成，写代结算日志"),
            ("TC-49", "电桩详情", "pile.detail 闲置/预约桩", "含 current_order 字段"),
        ],
    },
]

TOTAL_CASES = sum(len(m["cases"]) for m in MODULES)
