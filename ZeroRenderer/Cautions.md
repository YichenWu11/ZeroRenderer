## 目前存在的问题和注意事项

### 问题

* 结构臃肿，架构烂
* OnResize函数不能正常工作，窗口不能调整大小

### 注意事项

* ssaoPass 更新 ssaoPassCB 时要用到 mMainPassCB，在 updateMainPassCB 中把 mMainPassCB 传给了 ssaoPass (per frame)
