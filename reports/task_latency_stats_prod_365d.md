# Task Latency 统计报告 `prod_365d`

- csv: `/home/ubuntu/workspace/moomoo_CPP/strategy/task_system/bench_logs/task_latency.csv`
- 样本行数: 200（entry 0 / exit 200）

统计说明：**均值**、**中位数**用于中心趋势；**方差**、**标准差**用于波动；us 字段在表中已换算为 ms。
范围：**仅 C++ 执行段**（Executor claim → strategy → 报单/成交）；Worker prepare / `insert_ts_ms` 未开启时调度列为空，请用 `shell_start_ms` 起算的指标。

## Test 1 — 执行延时 · 开仓（entry）

| 指标 | n | 均值 | 中位数 | 方差 | 标准差 | 最小 | 最大 |
|------|--:|-----:|-------:|-----:|-------:|-----:|-----:|

## Test 1 — 执行延时 · 平仓（exit）

| 指标 | n | 均值 | 中位数 | 方差 | 标准差 | 最小 | 最大 |
|------|--:|-----:|-------:|-----:|-------:|-----:|-----:|
| 策略 shell 总耗时 | 100 | 4.3ms | 4.0ms | 5.1 | 2.2ms | 1.0ms | 15.0ms |
| SDK 调用（us→ms） | 100 | 0.0ms | 0.0ms | 0.0 | 0.0ms | 0.0ms | 0.4ms |
| 触发 → 报单发出 | 100 | 1788331445.5ms | 1788331485.2ms | 820496.3 | 905.8ms | 1788330240.4ms | 1788332549.6ms |
| 报单 → 成交 | 100 | 1788331445.5ms | 1788331485.2ms | 820496.3 | 905.8ms | 1788330240.4ms | 1788332549.6ms |
| 触发 → 成交 | 100 | 290214.3ms | 246216.0ms | 8365034681.2 | 91460.6ms | 235885.0ms | 525516.0ms |

## 逐条样本

- `bench_hk_02208_20260902_062341_001` leg=exit f_id=5195 sched_total=ms shell=ms api=1788330223707ms
- `bench_hk_03308_20260902_062341_002` leg=exit f_id=5196 sched_total=ms shell=ms api=1788330227141ms
- `bench_hk_02208_20260902_062341_001` leg=exit f_id=5195 sched_total=1788330236388ms shell=4ms api=1788330235897ms
- `bench_hk_03308_20260902_062341_002` leg=exit f_id=5196 sched_total=1788330241902ms shell=4ms api=1788330241440ms
- `bench_hk_02208_20260902_062341_003` leg=exit f_id=5197 sched_total=ms shell=ms api=1788330250248ms
- `bench_hk_03308_20260902_062341_004` leg=exit f_id=5198 sched_total=ms shell=ms api=1788330255781ms
- `bench_hk_02208_20260902_062341_003` leg=exit f_id=5197 sched_total=1788330263704ms shell=3ms api=1788330263240ms
- `bench_hk_03308_20260902_062341_004` leg=exit f_id=5198 sched_total=1788330265712ms shell=4ms api=1788330265257ms
- `bench_hk_02208_20260902_062341_005` leg=exit f_id=5199 sched_total=ms shell=ms api=1788330274055ms
- `bench_hk_03308_20260902_062341_006` leg=exit f_id=5200 sched_total=ms shell=ms api=1788330280105ms
- `bench_hk_02208_20260902_062341_005` leg=exit f_id=5199 sched_total=1788330288026ms shell=5ms api=1788330287295ms
- `bench_hk_03308_20260902_062341_006` leg=exit f_id=5200 sched_total=1788330294879ms shell=4ms api=1788330294161ms
- `bench_hk_02208_20260902_062341_007` leg=exit f_id=5201 sched_total=ms shell=ms api=1788330301176ms
- `bench_hk_03308_20260902_062341_008` leg=exit f_id=5202 sched_total=ms shell=ms api=1788330303690ms
- `bench_hk_02208_20260902_062341_007` leg=exit f_id=5201 sched_total=1788330312965ms shell=3ms api=1788330312501ms
- `bench_hk_03308_20260902_062341_008` leg=exit f_id=5202 sched_total=1788330318817ms shell=2ms api=1788330318352ms
- `bench_hk_02208_20260902_062341_009` leg=exit f_id=5203 sched_total=ms shell=ms api=1788330325537ms
- `bench_hk_03308_20260902_062341_010` leg=exit f_id=5204 sched_total=ms shell=ms api=1788330327560ms
- `bench_hk_02208_20260902_062341_009` leg=exit f_id=5203 sched_total=1788330337169ms shell=4ms api=1788330336703ms
- `bench_hk_03308_20260902_062341_010` leg=exit f_id=5204 sched_total=1788330346529ms shell=1ms api=1788330346064ms
- `bench_hk_02208_20260902_062341_011` leg=exit f_id=5205 sched_total=ms shell=ms api=1788330353756ms
- `bench_hk_03308_20260902_062341_012` leg=exit f_id=5206 sched_total=ms shell=ms api=1788330356522ms
- `bench_hk_02208_20260902_062341_011` leg=exit f_id=5205 sched_total=1788330364392ms shell=5ms api=1788330363922ms
- `bench_hk_03308_20260902_062341_012` leg=exit f_id=5206 sched_total=1788330366958ms shell=4ms api=1788330366477ms
- `bench_hk_02208_20260902_062341_013` leg=exit f_id=5207 sched_total=ms shell=ms api=1788330373561ms
- `bench_hk_03308_20260902_062341_014` leg=exit f_id=5208 sched_total=ms shell=ms api=1788330375583ms
- `bench_hk_02208_20260902_062341_013` leg=exit f_id=5207 sched_total=1788330384824ms shell=4ms api=1788330384361ms
- `bench_hk_03308_20260902_062341_014` leg=exit f_id=5208 sched_total=1788330390338ms shell=3ms api=1788330389875ms
- `bench_hk_02208_20260902_062341_015` leg=exit f_id=5209 sched_total=ms shell=ms api=1788330408292ms
- `bench_hk_03308_20260902_062341_016` leg=exit f_id=5210 sched_total=ms shell=ms api=1788330414312ms
- `bench_hk_02208_20260902_062341_015` leg=exit f_id=5209 sched_total=1788330423577ms shell=3ms api=1788330422826ms
- `bench_hk_03308_20260902_062341_016` leg=exit f_id=5210 sched_total=1788330429441ms shell=5ms api=1788330428713ms
- `bench_hk_02208_20260902_062341_017` leg=exit f_id=5211 sched_total=ms shell=ms api=1788330435917ms
- `bench_hk_03308_20260902_062341_018` leg=exit f_id=5212 sched_total=ms shell=ms api=1788330437954ms
- `bench_hk_02208_20260902_062341_017` leg=exit f_id=5211 sched_total=1788330447149ms shell=4ms api=1788330446683ms
- `bench_hk_03308_20260902_062341_018` leg=exit f_id=5212 sched_total=1788330452874ms shell=3ms api=1788330452407ms
- `bench_hk_02208_20260902_062341_019` leg=exit f_id=5213 sched_total=ms shell=ms api=1788330459957ms
- `bench_hk_03308_20260902_062341_020` leg=exit f_id=5214 sched_total=ms shell=ms api=1788330468139ms
- `bench_hk_02208_20260902_062341_019` leg=exit f_id=5213 sched_total=1788330474285ms shell=3ms api=1788330473822ms
- `bench_hk_03308_20260902_062341_020` leg=exit f_id=5214 sched_total=1788330476782ms shell=2ms api=1788330476326ms
- `bench_hk_02208_20260902_062341_021` leg=exit f_id=5215 sched_total=ms shell=ms api=1788330485241ms
- `bench_hk_03308_20260902_062341_022` leg=exit f_id=5216 sched_total=ms shell=ms api=1788330490946ms
- `bench_hk_02208_20260902_062341_021` leg=exit f_id=5215 sched_total=1788330498953ms shell=4ms api=1788330498488ms
- `bench_hk_03308_20260902_062341_022` leg=exit f_id=5216 sched_total=1788330500940ms shell=6ms api=1788330500474ms
- `bench_hk_02208_20260902_062341_023` leg=exit f_id=5217 sched_total=ms shell=ms api=1788330509020ms
- `bench_hk_03308_20260902_062341_024` leg=exit f_id=5218 sched_total=ms shell=ms api=1788330514574ms
- `bench_hk_02208_20260902_062341_023` leg=exit f_id=5217 sched_total=1788330522687ms shell=3ms api=1788330522220ms
- `bench_hk_03308_20260902_062341_024` leg=exit f_id=5218 sched_total=1788330532967ms shell=3ms api=1788330532506ms
- `bench_hk_02208_20260902_062341_025` leg=exit f_id=5219 sched_total=ms shell=ms api=1788330543049ms
- `bench_hk_03308_20260902_062341_026` leg=exit f_id=5220 sched_total=ms shell=ms api=1788330548603ms
- `bench_hk_02208_20260902_062341_025` leg=exit f_id=5219 sched_total=1788330557994ms shell=5ms api=1788330557275ms
- `bench_hk_03308_20260902_062341_026` leg=exit f_id=5220 sched_total=1788330563193ms shell=4ms api=1788330562468ms
- `bench_hk_02208_20260902_062341_027` leg=exit f_id=5221 sched_total=ms shell=ms api=1788330571486ms
- `bench_hk_03308_20260902_062341_028` leg=exit f_id=5222 sched_total=ms shell=ms api=1788330577026ms
- `bench_hk_02208_20260902_062341_027` leg=exit f_id=5221 sched_total=1788330585340ms shell=3ms api=1788330584872ms
- `bench_hk_03308_20260902_062341_028` leg=exit f_id=5222 sched_total=1788330591836ms shell=5ms api=1788330591376ms
- `bench_hk_02208_20260902_062341_029` leg=exit f_id=5223 sched_total=ms shell=ms api=1788330598417ms
- `bench_hk_03308_20260902_062341_030` leg=exit f_id=5224 sched_total=ms shell=ms api=1788330600954ms
- `bench_hk_02208_20260902_062341_029` leg=exit f_id=5223 sched_total=1788330611125ms shell=6ms api=1788330610656ms
- `bench_hk_03308_20260902_062341_030` leg=exit f_id=5224 sched_total=1788330617912ms shell=2ms api=1788330617443ms
- `bench_hk_02208_20260902_062341_031` leg=exit f_id=5225 sched_total=ms shell=ms api=1788330624894ms
- `bench_hk_03308_20260902_062341_032` leg=exit f_id=5226 sched_total=ms shell=ms api=1788330627705ms
- `bench_hk_02208_20260902_062341_031` leg=exit f_id=5225 sched_total=1788330637473ms shell=4ms api=1788330637005ms
- `bench_hk_03308_20260902_062341_032` leg=exit f_id=5226 sched_total=1788330643977ms shell=6ms api=1788330643506ms
- `bench_hk_02208_20260902_062341_033` leg=exit f_id=5227 sched_total=ms shell=ms api=1788330650564ms
- `bench_hk_03308_20260902_062341_034` leg=exit f_id=5228 sched_total=ms shell=ms api=1788330656796ms
- `bench_hk_02208_20260902_062341_033` leg=exit f_id=5227 sched_total=1788330663792ms shell=5ms api=1788330663316ms
- `bench_hk_03308_20260902_062341_034` leg=exit f_id=5228 sched_total=1788330669975ms shell=4ms api=1788330669510ms
- `bench_hk_02208_20260902_062341_035` leg=exit f_id=5229 sched_total=ms shell=ms api=1788330676675ms
- `bench_hk_03308_20260902_062341_036` leg=exit f_id=5230 sched_total=ms shell=ms api=1788330679777ms
- `bench_hk_02208_20260902_062341_035` leg=exit f_id=5229 sched_total=1788330690417ms shell=4ms api=1788330689686ms
- `bench_hk_03308_20260902_062341_036` leg=exit f_id=5230 sched_total=1788330696322ms shell=5ms api=1788330695606ms
- `bench_hk_02208_20260902_062341_037` leg=exit f_id=5231 sched_total=ms shell=ms api=1788330702742ms
- `bench_hk_03308_20260902_062341_038` leg=exit f_id=5232 sched_total=ms shell=ms api=1788330706253ms
- `bench_hk_02208_20260902_062341_037` leg=exit f_id=5231 sched_total=1788330716385ms shell=6ms api=1788330715922ms
- `bench_hk_03308_20260902_062341_038` leg=exit f_id=5232 sched_total=1788330722319ms shell=3ms api=1788330721855ms
- `bench_hk_02208_20260902_062341_039` leg=exit f_id=5233 sched_total=ms shell=ms api=1788330729033ms
- `bench_hk_03308_20260902_062341_040` leg=exit f_id=5234 sched_total=ms shell=ms api=1788330732548ms
- `bench_hk_02208_20260902_062341_039` leg=exit f_id=5233 sched_total=1788330741805ms shell=4ms api=1788330741345ms
- `bench_hk_03308_20260902_062341_040` leg=exit f_id=5234 sched_total=1788330748881ms shell=1ms api=1788330748411ms
- `bench_hk_02208_20260902_062341_041` leg=exit f_id=5235 sched_total=ms shell=ms api=1788330757601ms
- `bench_hk_03308_20260902_062341_042` leg=exit f_id=5236 sched_total=ms shell=ms api=1788330763680ms
- `bench_hk_02208_20260902_062341_041` leg=exit f_id=5235 sched_total=1788330771514ms shell=4ms api=1788330771028ms
- `bench_hk_03308_20260902_062341_042` leg=exit f_id=5236 sched_total=1788330774520ms shell=3ms api=1788330774052ms
- `bench_hk_02208_20260902_062341_043` leg=exit f_id=5237 sched_total=ms shell=ms api=1788330780963ms
- `bench_hk_03308_20260902_062341_044` leg=exit f_id=5238 sched_total=ms shell=ms api=1788330783969ms
- `bench_hk_02208_20260902_062341_043` leg=exit f_id=5237 sched_total=1788330791649ms shell=4ms api=1788330791181ms
- `bench_hk_03308_20260902_062341_044` leg=exit f_id=5238 sched_total=1788330794916ms shell=4ms api=1788330794453ms
- `bench_hk_02208_20260902_062341_045` leg=exit f_id=5239 sched_total=ms shell=ms api=1788330803673ms
- `bench_hk_03308_20260902_062341_046` leg=exit f_id=5240 sched_total=ms shell=ms api=1788330810071ms
- `bench_hk_02208_20260902_062341_045` leg=exit f_id=5239 sched_total=1788330818228ms shell=5ms api=1788330817514ms
- `bench_hk_03308_20260902_062341_046` leg=exit f_id=5240 sched_total=1788330821080ms shell=4ms api=1788330820619ms
- `bench_hk_02208_20260902_062341_047` leg=exit f_id=5241 sched_total=ms shell=ms api=1788330829823ms
- `bench_hk_03308_20260902_062341_048` leg=exit f_id=5242 sched_total=ms shell=ms api=1788330835800ms
- `bench_hk_02208_20260902_062341_047` leg=exit f_id=5241 sched_total=1788330847413ms shell=3ms api=1788330846943ms
- `bench_hk_03308_20260902_062341_048` leg=exit f_id=5242 sched_total=1788330851943ms shell=13ms api=1788330851478ms
- `bench_hk_02208_20260902_062341_049` leg=exit f_id=5243 sched_total=ms shell=ms api=1788330859455ms
- `bench_hk_03308_20260902_062341_050` leg=exit f_id=5244 sched_total=ms shell=ms api=1788330861995ms
- `bench_hk_02208_20260902_062341_049` leg=exit f_id=5243 sched_total=1788330870217ms shell=3ms api=1788330869757ms
- `bench_hk_03308_20260902_062341_050` leg=exit f_id=5244 sched_total=1788330872721ms shell=3ms api=1788330872254ms
- `bench_hk_03308_20260902_065439_002` leg=exit f_id=5246 sched_total=ms shell=ms api=1788332082514ms
- `bench_hk_02208_20260902_065439_001` leg=exit f_id=5245 sched_total=ms shell=ms api=1788332082491ms
- `bench_hk_02208_20260902_065439_001` leg=exit f_id=5245 sched_total=1788332092853ms shell=2ms api=1788332092390ms
- `bench_hk_03308_20260902_065439_002` leg=exit f_id=5246 sched_total=1788332096088ms shell=3ms api=1788332095611ms
- `bench_hk_03308_20260902_065439_004` leg=exit f_id=5248 sched_total=ms shell=ms api=1788332102582ms
- `bench_hk_02208_20260902_065439_003` leg=exit f_id=5247 sched_total=ms shell=ms api=1788332102549ms
- `bench_hk_03308_20260902_065439_004` leg=exit f_id=5248 sched_total=1788332111083ms shell=3ms api=1788332110617ms
- `bench_hk_02208_20260902_065439_003` leg=exit f_id=5247 sched_total=1788332111328ms shell=2ms api=1788332110616ms
- `bench_hk_02208_20260902_065439_005` leg=exit f_id=5249 sched_total=ms shell=ms api=1788332118660ms
- `bench_hk_03308_20260902_065439_006` leg=exit f_id=5250 sched_total=ms shell=ms api=1788332118641ms
- `bench_hk_02208_20260902_065439_005` leg=exit f_id=5249 sched_total=1788332123415ms shell=5ms api=1788332122701ms
- `bench_hk_03308_20260902_065439_006` leg=exit f_id=5250 sched_total=1788332123168ms shell=4ms api=1788332122687ms
- `bench_hk_02208_20260902_065439_007` leg=exit f_id=5251 sched_total=ms shell=ms api=1788332128561ms
- `bench_hk_03308_20260902_065439_008` leg=exit f_id=5252 sched_total=ms shell=ms api=1788332128757ms
- `bench_hk_03308_20260902_065439_008` leg=exit f_id=5252 sched_total=1788332145992ms shell=9ms api=1788332145294ms
- `bench_hk_02208_20260902_065439_007` leg=exit f_id=5251 sched_total=1788332146237ms shell=2ms api=1788332145271ms
- `bench_hk_02208_20260902_065439_009` leg=exit f_id=5253 sched_total=ms shell=ms api=1788332154847ms
- `bench_hk_03308_20260902_065439_010` leg=exit f_id=5254 sched_total=ms shell=ms api=1788332158037ms
- `bench_hk_02208_20260902_065439_009` leg=exit f_id=5253 sched_total=1788332165240ms shell=6ms api=1788332164769ms
- `bench_hk_03308_20260902_065439_010` leg=exit f_id=5254 sched_total=1788332165489ms shell=3ms api=1788332164787ms
- `bench_hk_03308_20260902_065439_012` leg=exit f_id=5256 sched_total=ms shell=ms api=1788332173291ms
- `bench_hk_02208_20260902_065439_011` leg=exit f_id=5255 sched_total=ms shell=ms api=1788332173283ms
- `bench_hk_02208_20260902_065439_011` leg=exit f_id=5255 sched_total=1788332181384ms shell=5ms api=1788332180922ms
- `bench_hk_03308_20260902_065439_012` leg=exit f_id=5256 sched_total=1788332181623ms shell=2ms api=1788332180936ms
- `bench_hk_02208_20260902_065439_013` leg=exit f_id=5257 sched_total=ms shell=ms api=1788332191176ms
- `bench_hk_03308_20260902_065439_014` leg=exit f_id=5258 sched_total=ms shell=ms api=1788332191181ms
- `bench_hk_03308_20260902_065439_014` leg=exit f_id=5258 sched_total=1788332210079ms shell=4ms api=1788332209110ms
- `bench_hk_02208_20260902_065439_013` leg=exit f_id=5257 sched_total=1788332209832ms shell=5ms api=1788332209128ms
- `bench_hk_02208_20260902_065439_015` leg=exit f_id=5259 sched_total=ms shell=ms api=1788332217032ms
- `bench_hk_03308_20260902_065439_016` leg=exit f_id=5260 sched_total=ms shell=ms api=1788332220447ms
- `bench_hk_02208_20260902_065439_015` leg=exit f_id=5259 sched_total=1788332227685ms shell=4ms api=1788332227214ms
- `bench_hk_03308_20260902_065439_016` leg=exit f_id=5260 sched_total=1788332227927ms shell=4ms api=1788332227254ms
- `bench_hk_02208_20260902_065439_017` leg=exit f_id=5261 sched_total=ms shell=ms api=1788332235257ms
- `bench_hk_03308_20260902_065439_018` leg=exit f_id=5262 sched_total=ms shell=ms api=1788332235260ms
- `bench_hk_02208_20260902_065439_017` leg=exit f_id=5261 sched_total=1788332244051ms shell=6ms api=1788332243331ms
- `bench_hk_03308_20260902_065439_018` leg=exit f_id=5262 sched_total=1788332243797ms shell=5ms api=1788332243323ms
- `bench_hk_02208_20260902_065439_019` leg=exit f_id=5263 sched_total=ms shell=ms api=1788332253215ms
- `bench_hk_03308_20260902_065439_020` leg=exit f_id=5264 sched_total=ms shell=ms api=1788332253437ms
- `bench_hk_02208_20260902_065439_019` leg=exit f_id=5263 sched_total=1788332270316ms shell=4ms api=1788332269444ms
- `bench_hk_03308_20260902_065439_020` leg=exit f_id=5264 sched_total=1788332270559ms shell=5ms api=1788332269417ms
- `bench_hk_02208_20260902_065439_021` leg=exit f_id=5265 sched_total=ms shell=ms api=1788332279619ms
- `bench_hk_03308_20260902_065439_022` leg=exit f_id=5266 sched_total=ms shell=ms api=1788332282807ms
- `bench_hk_02208_20260902_065439_021` leg=exit f_id=5265 sched_total=1788332289968ms shell=4ms api=1788332289495ms
- `bench_hk_03308_20260902_065439_022` leg=exit f_id=5266 sched_total=1788332290211ms shell=15ms api=1788332289517ms
- `bench_hk_02208_20260902_065439_023` leg=exit f_id=5267 sched_total=ms shell=ms api=1788332299513ms
- `bench_hk_03308_20260902_065439_024` leg=exit f_id=5268 sched_total=ms shell=ms api=1788332299524ms
- `bench_hk_02208_20260902_065439_023` leg=exit f_id=5267 sched_total=1788332306115ms shell=4ms api=1788332305650ms
- `bench_hk_03308_20260902_065439_024` leg=exit f_id=5268 sched_total=1788332306364ms shell=2ms api=1788332305670ms
- `bench_hk_02208_20260902_065439_025` leg=exit f_id=5269 sched_total=ms shell=ms api=1788332315507ms
- `bench_hk_03308_20260902_065439_026` leg=exit f_id=5270 sched_total=ms shell=ms api=1788332315707ms
- `bench_hk_02208_20260902_065439_025` leg=exit f_id=5269 sched_total=1788332328183ms shell=4ms api=1788332327716ms
- `bench_hk_03308_20260902_065439_026` leg=exit f_id=5270 sched_total=1788332331680ms shell=4ms api=1788332330965ms
- `bench_hk_03308_20260902_065439_028` leg=exit f_id=5272 sched_total=ms shell=ms api=1788332341844ms
- `bench_hk_02208_20260902_065439_027` leg=exit f_id=5271 sched_total=ms shell=ms api=1788332341634ms
- `bench_hk_02208_20260902_065439_027` leg=exit f_id=5271 sched_total=1788332352356ms shell=4ms api=1788332351876ms
- `bench_hk_03308_20260902_065439_028` leg=exit f_id=5272 sched_total=1788332355574ms shell=11ms api=1788332355094ms
- `bench_hk_02208_20260902_065439_029` leg=exit f_id=5273 sched_total=ms shell=ms api=1788332361925ms
- `bench_hk_03308_20260902_065439_030` leg=exit f_id=5274 sched_total=ms shell=ms api=1788332361937ms
- `bench_hk_02208_20260902_065439_029` leg=exit f_id=5273 sched_total=1788332372340ms shell=6ms api=1788332371872ms
- `bench_hk_03308_20260902_065439_030` leg=exit f_id=5274 sched_total=1788332372584ms shell=5ms api=1788332371880ms
- `bench_hk_02208_20260902_065439_031` leg=exit f_id=5275 sched_total=ms shell=ms api=1788332386144ms
- `bench_hk_03308_20260902_065439_032` leg=exit f_id=5276 sched_total=ms shell=ms api=1788332386112ms
- `bench_hk_02208_20260902_065439_031` leg=exit f_id=5275 sched_total=1788332396400ms shell=6ms api=1788332395941ms
- `bench_hk_03308_20260902_065439_032` leg=exit f_id=5276 sched_total=1788332396686ms shell=5ms api=1788332395967ms
- `bench_hk_02208_20260902_065439_033` leg=exit f_id=5277 sched_total=ms shell=ms api=1788332402177ms
- `bench_hk_03308_20260902_065439_034` leg=exit f_id=5278 sched_total=ms shell=ms api=1788332405374ms
- `bench_hk_02208_20260902_065439_033` leg=exit f_id=5277 sched_total=1788332414485ms shell=2ms api=1788332414010ms
- `bench_hk_03308_20260902_065439_034` leg=exit f_id=5278 sched_total=1788332414731ms shell=4ms api=1788332414215ms
- `bench_hk_02208_20260902_065439_035` leg=exit f_id=5279 sched_total=ms shell=ms api=1788332424178ms
- `bench_hk_03308_20260902_065439_036` leg=exit f_id=5280 sched_total=ms shell=ms api=1788332424188ms
- `bench_hk_03308_20260902_065439_036` leg=exit f_id=5280 sched_total=1788332435000ms shell=5ms api=1788332434296ms
- `bench_hk_02208_20260902_065439_035` leg=exit f_id=5279 sched_total=1788332434750ms shell=13ms api=1788332434284ms
- `bench_hk_03308_20260902_065439_038` leg=exit f_id=5282 sched_total=ms shell=ms api=1788332446579ms
- `bench_hk_02208_20260902_065439_037` leg=exit f_id=5281 sched_total=ms shell=ms api=1788332446601ms
- `bench_hk_03308_20260902_065439_038` leg=exit f_id=5282 sched_total=1788332454870ms shell=1ms api=1788332454403ms
- `bench_hk_02208_20260902_065439_037` leg=exit f_id=5281 sched_total=1788332458080ms shell=4ms api=1788332457611ms
- `bench_hk_03308_20260902_065439_040` leg=exit f_id=5284 sched_total=ms shell=ms api=1788332464535ms
- `bench_hk_02208_20260902_065439_039` leg=exit f_id=5283 sched_total=ms shell=ms api=1788332464498ms
- `bench_hk_02208_20260902_065439_039` leg=exit f_id=5283 sched_total=1788332472947ms shell=6ms api=1788332472476ms
- `bench_hk_03308_20260902_065439_040` leg=exit f_id=5284 sched_total=1788332473194ms shell=7ms api=1788332472482ms
- `bench_hk_03308_20260902_065439_042` leg=exit f_id=5286 sched_total=ms shell=ms api=1788332480493ms
- `bench_hk_02208_20260902_065439_041` leg=exit f_id=5285 sched_total=ms shell=ms api=1788332480484ms
- `bench_hk_03308_20260902_065439_042` leg=exit f_id=5286 sched_total=1788332485306ms shell=3ms api=1788332484589ms
- `bench_hk_02208_20260902_065439_041` leg=exit f_id=5285 sched_total=1788332485064ms shell=5ms api=1788332484574ms
- `bench_hk_03308_20260902_065439_044` leg=exit f_id=5288 sched_total=ms shell=ms api=1788332490643ms
- `bench_hk_02208_20260902_065439_043` leg=exit f_id=5287 sched_total=ms shell=ms api=1788332490653ms
- `bench_hk_03308_20260902_065439_044` leg=exit f_id=5288 sched_total=1788332497514ms shell=4ms api=1788332496768ms
- `bench_hk_02208_20260902_065439_043` leg=exit f_id=5287 sched_total=1788332497264ms shell=4ms api=1788332496759ms
- `bench_hk_03308_20260902_065439_046` leg=exit f_id=5290 sched_total=ms shell=ms api=1788332511828ms
- `bench_hk_02208_20260902_065439_045` leg=exit f_id=5289 sched_total=ms shell=ms api=1788332511846ms
- `bench_hk_02208_20260902_065439_045` leg=exit f_id=5289 sched_total=1788332520279ms shell=4ms api=1788332519794ms
- `bench_hk_03308_20260902_065439_046` leg=exit f_id=5290 sched_total=1788332520535ms shell=2ms api=1788332519820ms
- `bench_hk_02208_20260902_065439_047` leg=exit f_id=5291 sched_total=ms shell=ms api=1788332526901ms
- `bench_hk_03308_20260902_065439_048` leg=exit f_id=5292 sched_total=ms shell=ms api=1788332526889ms
- `bench_hk_03308_20260902_065439_048` leg=exit f_id=5292 sched_total=1788332535694ms shell=4ms api=1788332535005ms
- `bench_hk_02208_20260902_065439_047` leg=exit f_id=5291 sched_total=1788332535451ms shell=3ms api=1788332534988ms
- `bench_hk_02208_20260902_065439_049` leg=exit f_id=5293 sched_total=ms shell=ms api=1788332542814ms
- `bench_hk_03308_20260902_065439_050` leg=exit f_id=5294 sched_total=ms shell=ms api=1788332542819ms
- `bench_hk_03308_20260902_065439_050` leg=exit f_id=5294 sched_total=1788332547353ms shell=4ms api=1788332546878ms
- `bench_hk_02208_20260902_065439_049` leg=exit f_id=5293 sched_total=1788332547596ms shell=2ms api=1788332546886ms
