// index.ts
interface EntryCard {
  title: string
  desc: string
  path: string
}

interface IndexPageData {
  appName: string
  slogan: string
  loginText: string
  cards: EntryCard[]
}

Page<IndexPageData>({
  data: {
    appName: '小程序骨架',
    slogan: '先搭结构，再接业务',
    loginText: '未登录',
    cards: [
      {
        title: '工作台',
        desc: '统一放业务入口和常用操作',
        path: '/pages/workbench/index',
      },
      {
        title: '组件示例',
        desc: '用于沉淀可复用 UI 模块',
        path: '/pages/components/index',
      },
      {
        title: '开发日志',
        desc: '查看本地启动历史和调试记录',
        path: '/pages/logs/logs',
      },
    ],
  },

  onShow() {
    const app = getApp<IAppOption>()
    const userInfo = app.globalData.userInfo
    this.setData({
      loginText: userInfo ? userInfo.nickName : '未登录',
    })
  },

  goToPage(event: WechatMiniprogram.BaseEvent) {
    const path = String(event.currentTarget.dataset.path || '')
    if (!path) {
      return
    }

    wx.navigateTo({
      url: path,
    })
  },
})
