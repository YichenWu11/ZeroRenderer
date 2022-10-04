## 目前存在的问题和注意事项

### 问题

* 底层架构不是很好

### 注意事项

* ssaoPass 更新 ssaoPassCB 时要用到 mMainPassCB，在 updateMainPassCB 中把 mMainPassCB 传给了 ssaoPass (per frame)
