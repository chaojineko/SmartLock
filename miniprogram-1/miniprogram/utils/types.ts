export interface ApiResponse<T> {
  code: number
  message: string
  data: T
}

export interface UserProfile {
  id: string
  nickname: string
  avatarUrl?: string
}

export interface CommonItem {
  id: string
  title: string
  createdAt: string
}

export interface WechatLoginPayload {
  code: string
  nickName: string
  avatarUrl: string
}

export interface WechatLoginResult {
  userId?: string
  token?: string
  nickname?: string
  avatarUrl?: string
}

export interface CloudFunctionResponse<T> {
  code?: number
  message?: string
  data?: T
}

export type GrantTimeMode = 'duration' | 'window'

export interface CreateGrantPayload {
  lockId: string
  mode: GrantTimeMode
  durationMin?: number
  startAt?: number
  endAt?: number
}

export interface CreateGrantResult {
  grantId?: string
  grantToken: string
  validFrom?: number | string
  expireAt: number | string
  qrDataUrl?: string
}

export interface ActiveGrantItem {
  grantId: string
  lockId: string
  grantToken: string
  validFrom: number
  expireAt: number
  qrDataUrl?: string
}
