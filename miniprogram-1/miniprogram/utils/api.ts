import { DEFAULT_ENV, ENV } from './config'
import type {
  ApiResponse,
  CommonItem,
  UserProfile,
  WechatLoginPayload,
  WechatLoginResult,
} from './types'

interface RequestOptions {
  url: string
  method: 'GET' | 'POST' | 'DELETE'
  data?: WechatMiniprogram.IAnyObject | string | ArrayBuffer
}

const request = <T>(options: RequestOptions): Promise<ApiResponse<T>> => {
  return new Promise((resolve, reject) => {
    wx.request({
      url: `${ENV[DEFAULT_ENV].baseUrl}${options.url}`,
      method: options.method,
      data: options.data,
      timeout: 10000,
      success: (res) => {
        if (res.statusCode >= 200 && res.statusCode < 300) {
          resolve(res.data as ApiResponse<T>)
          return
        }

        const message = `请求失败，状态码 ${res.statusCode}`
        reject(new Error(message))
      },
      fail: (err) => {
        reject(new Error(err.errMsg || '网络请求失败'))
      },
    })
  })
}

export const fetchProfile = () => {
  return request<UserProfile>({
    url: '/me/profile',
    method: 'GET',
  })
}

export const fetchCommonList = () => {
  return request<CommonItem[]>({
    url: '/common/list',
    method: 'GET',
  })
}

export const deleteCommonItem = (id: string) => {
  return request<null>({
    url: `/common/${id}`,
    method: 'DELETE',
  })
}

export const wechatLogin = (payload: WechatLoginPayload) => {
  return request<WechatLoginResult>({
    url: '/auth/wechat/login',
    method: 'POST',
    data: payload,
  })
}
