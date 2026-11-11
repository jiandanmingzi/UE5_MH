# 键鼠与状态操作提示

更新：2026-11-11。键鼠与手柄共用原Enhanced Input动作、InputProfile和Combo；组合键宽限期、RT短按松开、方向快照和预输入继续使用现有系统。

| 键鼠 | 对应手柄输入 |
|---|---|
| W / A / S / D | 移动前 / 左 / 后 / 右 |
| 鼠标左键 | Y |
| 鼠标右键 | B |
| 空格 | A |
| 左/右Ctrl | RT |
| R | LT |
| 左/右Shift | RB |

映射资产：`/Game/Input/Contexts/IMC_MHGZ_Demo`。`IA_Move`使用Cumulative累加，W/S及A/D可互相抵消；W/S用轴交换，A/S取负值。原Gamepad映射与MouseLook映射保留，旧T→RT替换为Ctrl。

## 操作面板

操作提示背景完全透明，只绘制标题和按键、招式文字。

主HUD左侧中部显示当前状态的输入结果。实际键盘按下、鼠标移动/点击切换为键鼠名称；手柄按钮或超过0.2的模拟轴输入切回手柄。释放旧按键、回中的轴与微小摇杆漂移不切换设备。

按A、Y、B、RT、LT、RB排序；组合内部同样保持A、Y、B顺序。每个逻辑输入优先显示无方向结果，再显示不同结果的前/后/左/右派生；同一招式的多方向等价入口只显示一个代表，优先前方向。`前`等方向含义与现有角色相对方向判定一致，不把它另解释为固定W键。

面板读取InputProfile、当前Combo状态、标签、优先级和接受窗口，不触发输入或提前激活GA。Combo中要求窗口的后继只在对应窗口开放时列出；空中会按Falling/舞踏来源等状态更新。LT相关预览只在临时快照中补齐假设瞄准标签，不给实际ASC授予标签。翻滚、空回和收刀读取已授予的直接GA及其可激活门禁；收刀地面另显示疾跑，持刀显示猎虫瞄准。

RT短按松开动作显示`（短按松开）`。除用户列出的Idle项目外，当前实际可匹配的虫印斩、瞄准、收刀等也会显示。

## 名称与维护

GA新增中文字段`Ability → Input → 操作提示名称`。留空时沿用已有虫棍拼音类名的中文对照；新增招式或特殊显示名可直接在其GA填写，不必修改Combo执行逻辑。面板由常驻`WBP_HUD`的原生父类绘制，HUD最多每0.1真实秒更新一次；无须手动生成或重接Widget资产，换Pawn会清空旧提示。

输入资产维护脚本：`Scripts/AssetOrganization/configure_desktop_input.py`，默认只读回查。首次Apply已完成，禁止重放；关闭编辑器时由Python Commandlet运行，不加`-MHGZDesktopInputApply`。编辑器已打开时用`Scripts/ue_remote.py`运行同一脚本的默认回查。备份、真实Combo场景、定向检查及720p/1080p预览见`Saved/Maintenance/DesktopInput20261111/`。
