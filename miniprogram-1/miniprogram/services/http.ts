import { DEFAULT_ENV, ENV } from '../utils/config'

interface HttpOptions<TData = unknown> {
  url: string
  method?: 'GET' | 'POST' | 'PUT' | 'DELETE'
  data?: TData & (WechatMiniprogram.IAnyObject | string | ArrayBuffer)
  header?: Record<string, string>
}

export const httpRequest = <TResponse, TData = unknown>(options: HttpOptions<TData>) => {
  return new Promise<TResponse>((resolve, reject) => {
    wx.request({
      url: `${ENV[DEFAULT_ENV].baseUrl}${options.url}`,
      method: options.method || 'GET',
      data: options.data,
      header: options.header,
      timeout: 10000,
      success: (res) => {
        if (res.statusCode >= 200 && res.statusCode < 300) {
          resolve(res.data as TResponse)
          return
        }
        reject(new Error(`请求失败: ${res.statusCode}`))
      },
      fail: (err) => {
        reject(new Error(err.errMsg || '网络错误'))
      },
    })
  })
}
