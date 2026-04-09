import { appStore } from '../../store/index'
import { wechatLogin } from '../../utils/api'

interface ProfileData {
  nickname: string
  userId: string
  isLogin: boolean
  isLogging: boolean
}

Page<ProfileData>({
  data: {
    nickname: '未登录用户',
    userId: 'N/A',
    isLogin: false,
    isLogging: false,
  },

  onShow() {
    const state = appStore.getState()
    this.setData({
      nickname: state.nickname,
      userId: state.userId,
      isLogin: state.isLogin,
    })
  },

  getWxProfile() {
    return new Promise<WechatMiniprogram.GetUserProfileSuccessCallbackResult>((resolve, reject) => {
      wx.getUserProfile({
        desc: '用于完善会员资料',
        success: (res) => resolve(res),
        fail: (err) => reject(new Error(err.errMsg || '获取微信资料失败')),
      })
    })
  },

  getWxCode() {
    return new Promise<string>((resolve, reject) => {
      wx.login({
        success: (res) => {
          if (res.code) {
            resolve(res.code)
            return
          }

          reject(new Error('微信登录码为空'))
        },
        fail: (err) => reject(new Error(err.errMsg || '微信登录失败')),
      })
    })
  },

  async loginByWechat() {
    if (this.data.isLogging) {
      return
    }

    this.setData({ isLogging: true })

    try {
      const profileRes = await this.getWxProfile()
      const userInfo = profileRes.userInfo
      const code = await this.getWxCode()

      let backendUserId = 'WX-LOCAL'
      try {
        const loginRes = await wechatLogin({
          code,
          nickName: userInfo.nickName,
          avatarUrl: userInfo.avatarUrl,
        })
        if (loginRes.data && loginRes.data.userId) {
          backendUserId = loginRes.data.userId
        }
      } catch (error) {
        const message = error instanceof Error ? error.message : '后端登录不可用'
        wx.showToast({ title: `${message}，已本地登录`, icon: 'none' })
      }

      appStore.setLogin(true)
      appStore.setNickname(userInfo.nickName)
      appStore.setUserId(backendUserId)

      const app = getApp<IAppOption>()
      app.globalData.userInfo = userInfo

      this.onShow()
      wx.showToast({ title: '微信登录成功', icon: 'success' })
    } catch (error) {
      const message = error instanceof Error ? error.message : '登录失败'
      wx.showToast({ title: message, icon: 'none' })
    } finally {
      this.setData({ isLogging: false })
    }
  },

  logout() {
    appStore.setLogin(false)
    appStore.setNickname('未登录用户')
    appStore.setUserId('N/A')

    const app = getApp<IAppOption>()
    delete app.globalData.userInfo

    this.onShow()
    wx.showToast({ title: '已退出', icon: 'none' })
  },
})
