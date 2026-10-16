# 六组地面攻击入口修正

2026-10-15更新：**用户否定第14帧入口，Idle已统一使用前部原`Entry`；末尾两个Idle专用片段和Section已移除。** 原六组入口的代码、资产保存、独立磁盘重载和三项定向技术检查已完成；不因此重开全量验收。原作录制来源及推断边界见 `Saved/Maintenance/GroundAttackEntries/report.md`；原修改证据和备份见 `Saved/Maintenance/GroundAttackEntryFix`。

## 当前配置

所有源序列均为60FPS；下表的2×是实际播放倍率，不是把资源帧率改为120FPS。入口的0/7/67指源序列帧，不是Montage显示时间轴的帧。

| 指定路线 | 已保存的动画路线 | 播放与混合 |
|---|---|---|
| 站立、后撤→上捞 | `Core`直接进入120第0帧；共享主体保持至原末帧 | 有效2×；目标Montage既有混入0.05s保留 |
| 飞身、普通突进→上捞 | `Entry_From_FeiShenYueRu`播放完整308，再转`Core_From_Bridge`进入120第7帧 | 两段均有效2×；内部惯性混合0.05s |
| 反向突进→上捞 | 独立`Entry_From_TuJinHuiXuanZhan_Back`播放完整305，再转第0帧`Core` | 两段均有效2×；内部惯性混合0.05s |
| 上捞→袈裟 | GA专属边改用`Entry_From_TuCi`，119从第0帧连续播放；移除额外307相位 | 有效2×，不新增冻结；源/目标已有Montage混入混出保留 |
| 拔刀斩 | 原300完整段保留；后续102从第67源帧进入，原结束源帧不变 | 两段仍有效2×；内部惯性混合0.05s |
| 站立→突进回旋 | 与其他来源共用前部`Entry`：306第0–30帧→160第35–55帧；按住接`Charge`，提前松键则按既有规则接`Core` | Entry与Core有效2×；Charge第55–67帧的两秒拉伸不变 |

横扫→上捞依旧是305→120第7帧，帧/倍率不变；它也共用上捞Montage的0.05s内部混合。原Core主体从15帧开始的错误已消除，`Core_From_Bridge`是在同一完整120片段内的第7帧Section，不是新增停帧。内部混合最初为0.06s，后续按用户要求随全部多序列Montage统一为0.05s；入口修正脚本的历史检查点不代表当前混合值。

拔刀102的67帧来自约67–68帧的根轨道拟合候选，不是原作逐骨帧直接读取真值。308原资源18帧/0.3s，按有效2×完整播放0.15s；录制动作号相位约0.158–0.167s含采样/切号边界，不能把该相位直接当成资源长度或再补保持帧。其余视觉Blend时长同样不是原作录制真值。

## 蓄力兼容

### 当前统一入口（2026-10-15）

第14帧只是姿态指标的最小值，用户目视仍发现武器瞬间脱离右手，因此该试验已否定。
不再使用`Entry_From_Idle`/`Release_From_Idle`；它们对应的末尾两个片段也已删除，不仅是取消引用。
Idle边移除专用GA入口覆盖，回退现有默认`Entry`；清除失效的未蓄力续段映射。

当前物理顺序仅为前部原306入口、160第35–55帧、Charge第55–67帧、Core第67帧至末帧，
共四片段、三个Section（Entry/Charge/Core）。不在收招后追加可选起手；以后确需独立入口时
优先放在前部并同步检查所有Section/通知/标签窗口，不默认向Montage末尾拼接。

按住：`Entry → Charge → Core`；Entry内已松开Y/B：`Entry → Core`；Charge内松开：立即进入Core。
与原共享Entry行为一致，**不再保留Idle独有的正常速度55–67续段**。通用可选续段代码仍保留，
但该GA不配置它；两秒最大蓄力、全部Y/B松开规则和反击逻辑不变。

七个通知的GUID、轨道、配置与原动画相位保留：ChargeStarted对齐Charge起点；反击窗口从Core起点
开放；三段AttackCollision、ComboWindow和DodgeAcceptWindow均留在Core，闪避窗口末端不超过实际末帧。
GA除两处入口/续段映射外的导出内容保持一致，包括伤害和标签配置；未添加强制Idle或新输入限制。

只备份/保存一份Montage与一份GA。事务及通知明细见`Saved/Maintenance/TuJinSharedEntry20261015/`。
旧14帧试验仅保留历史证据，不再运行它的Apply/Verify。当前原生检查源已同步共享Entry；本轮不为
资产整理编译C++或执行全量回归，运行时观感仍以局部PIE为准。

## 308资产与通知

原308资产Root Motion关闭且RateScale=1，因此新增常驻副本：`/Game/Weapons/InsectGlaive/Anims/Sequences/Generated/AS_IG_FeiShenYueRuShangLao_RM`。只在副本启用Root Motion，原轨道/根方向/长度/RateScale保持；Montage片段AnimPlayRate=2。没有重新做动画、重定向或纠正坐标轴。三个Root基底核对及复制后所有骨骼采样一致性通过。

上捞重新构成为独立305反向入口、原305横扫入口、308入口及完整120主体。反向入口复制原305的碰撞窗口和RootMotionPhase，使用新的Notify GUID和独立通知对象，伤害索引/配置不变；既有Core碰撞、Combo、Dodge等通知的身份和源序列相位保留。各入口RootMotionPhase重新覆盖自己的真实片段，不横跨未播放分支。袈裟移除307专属RootMotionPhase，其他通知按原源帧重定位。拔刀裁切后通知同步重定位，不改300。

## 维护边界

“人工保持段值得统一处理”不是新增总前摇的理由，也不是全项目批量删除指令。应按每条录制结果配置正确序列、入口源帧与有效倍率；这样自然不应再多塞额外冻结。混合只负责连续推进中的姿态接缝，不代替帧率/入口修正，不用新保持或一帧桥接掩盖错误。本轮仅处理上表，不改其他招式保持段、不删除未引用历史Sequence资产、不改四连或横扫→虫印。

仅保存四份Montage、两份GA及一份新Sequence，DA_IG_Combo、GA_IG_ShangLao、ABP和原导入序列均未保存。保存前的精确备份位于 `Saved/AssetBackups/GroundAttackEntries-1791976951393858300`。

工具为 [apply_ground_attack_entries.py](../../Scripts/AssetOrganization/apply_ground_attack_entries.py)，默认只读；检测已有`applied.json`后仅验证，不盲目重建。依赖既有AnimationSeamLibrary扩展的显式单Slot配方和通知重定位；故障先核对Saved报告与磁盘状态，不重复Apply。没有提交/推送Git或运行全量回归。

后续已批准的视觉桥接清理移除了突进旧Entry内的`EntryToCoreBlend`，其物理片段数由8变7，Section/通知整体扣除0.133333s，但源裁切、蓄力两秒及站立入口路由不变。当前只读验证识别`Saved/Maintenance/MontageBridgeRemoval`检查点并采用清理后配方；原生GroundEntries检查源码也同步了新片段索引和0.05s，不能用旧编译版本的8片段/0.06s断言判定当前资产错误。清理详情见[内部混合说明](montage-segment-blend.md)。

再后续攻击Hold清理已移除突进EntryHold（该批完成时6片段，Charge仍两秒）和袈裟TuJinEntryHold（2片段，突进来源直接映射`Entry_From_TuCi`），未改变剩余原动画帧。该批检查点为`Saved/Maintenance/AttackEntryHoldRemoval`；后来突进统一Entry已进一步移除末尾两片段，**当前突进4片段**，以本页开头及`Saved/Maintenance/TuJinSharedEntry20261015`为准。旧8/7/6片段记录是阶段历史，不是待恢复结构；原生检查源已同步最新状态，下次常规编译生效。

本轮只需局部PIE观察Idle→突进的起手、普通松键与按住蓄力；其他入口保留既有状态，不要求因这次整理重验六组或其他已签收系统。
