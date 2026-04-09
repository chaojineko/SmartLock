export interface AppStoreState {
  env: 'dev' | 'prod'
  isLogin: boolean
  nickname: string
  userId: string
}

const STORAGE_KEY = 'app-store-state'

const state: AppStoreState = {
  env: 'dev',
  isLogin: false,
  nickname: '未登录用户',
  userId: 'N/A',
}

const persist = () => {
  wx.setStorageSync(STORAGE_KEY, state)
}

export const appStore = {
  getState: () => state,
  restore: () => {
    const local = wx.getStorageSync(STORAGE_KEY) as Partial<AppStoreState> | undefined
    if (!local) {
      return
    }

    state.env = local.env || 'dev'
    state.isLogin = Boolean(local.isLogin)
    state.nickname = local.nickname || '未登录用户'
    state.userId = local.userId || 'N/A'
  },
  setEnv: (env: AppStoreState['env']) => {
    state.env = env
    persist()
  },
  setLogin: (isLogin: boolean) => {
    state.isLogin = isLogin
    if (!isLogin) {
      state.nickname = '未登录用户'
      state.userId = 'N/A'
    }
    persist()
  },
  setNickname: (nickname: string) => {
    state.nickname = nickname || '未登录用户'
    persist()
  },
  setUserId: (userId: string) => {
    state.userId = userId || 'N/A'
    persist()
  },
}
