## 目前存在的问题和注意事项

### 问题

* 底层架构不是很好

### 注意事项

* ssaoPass 更新 ssaoPassCB 时要用到 mMainPassCB，mainPass 的 Update 中把 mMainPassCB 传给了 ssaoPass (per frame)
* 由于更新 mMainPassCB 比其他 Pass 要用到更多参数，为了保持 RenderPass 调用的统一性，把 mainPass 的 Update 
需要的参数在 ZeroRenderer::Update 中传给了 mainPass
* 修改视口要同时修改 d3dApp 和 ssao 中的视口(ViewPort)