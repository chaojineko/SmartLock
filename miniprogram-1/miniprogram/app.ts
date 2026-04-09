// app.ts
import { CLOUD_ENV_ID } from './utils/config'
import { appStore } from './store/index'

App<IAppOption>({
  globalData: {},
  onLaunch() {
    if (wx.cloud) {
      wx.cloud.init({
        env: CLOUD_ENV_ID,
        traceUser: true,
      })
    } else {
      console.warn('wx.cloud is not available')
    }

    appStore.restore()

    // 展示本地存储能力
    const logs = wx.getStorageSync('logs') || []
    logs.unshift(Date.now())
    wx.setStorageSync('logs', logs)

    // 登录
    wx.login({
      success: res => {
        console.log('wx.login code', res.code)
        // 发送 res.code 到后台换取 openId, sessionKey, unionId
      },
    })
  },
})