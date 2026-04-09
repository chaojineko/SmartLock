import type { ActiveGrantItem, CloudFunctionResponse, CreateGrantPayload, CreateGrantResult } from '../utils/types'

const normalizeGrantResult = (result: unknown): CreateGrantResult => {
  if (!result || typeof result !== 'object') {
    throw new Error('云函数返回为空')
  }

  const withData = result as CloudFunctionResponse<CreateGrantResult>
  if (withData.data) {
    if (typeof withData.code === 'number' && withData.code !== 0) {
      throw new Error(withData.message || '云函数返回错误')
    }

    if (!withData.data.grantToken || !withData.data.expireAt) {
      throw new Error('云函数返回缺少 grantToken/expireAt，请确认已部署最新 createGrant 代码')
    }

    return withData.data
  }

  const fallback = result as Partial<CreateGrantResult>
  if (!fallback.grantToken || !fallback.expireAt) {
    throw new Error('云函数返回不是二维码结构，请检查 createGrant 是否仍为 Hello World 模板')
  }

  return fallback as CreateGrantResult
}

export const createGrant = async (payload: CreateGrantPayload) => {
  if (!wx.cloud) {
    throw new Error('云开发未初始化')
  }

  const res = await wx.cloud.callFunction({
    name: 'createGrant',
    data: payload,
  })

  return normalizeGrantResult(res.result)
}

const normalizeArrayResult = <T>(result: unknown) => {
  if (!result || typeof result !== 'object') {
    throw new Error('云函数返回为空')
  }

  const withData = result as CloudFunctionResponse<T[]>
  if (withData.data) {
    if (typeof withData.code === 'number' && withData.code !== 0) {
      throw new Error(withData.message || '云函数返回错误')
    }
    return withData.data
  }

  return result as T[]
}

export const listActiveGrants = async () => {
  if (!wx.cloud) {
    throw new Error('云开发未初始化')
  }

  const res = await wx.cloud.callFunction({
    name: 'listActiveGrants',
  })

  return normalizeArrayResult<ActiveGrantItem>(res.result)
}

export const revokeGrant = async (grantId: string) => {
  if (!wx.cloud) {
    throw new Error('云开发未初始化')
  }

  const res = await wx.cloud.callFunction({
    name: 'revokeGrant',
    data: { grantId },
  })

  const result = res.result as CloudFunctionResponse<{ success: boolean }>
  if (typeof result.code === 'number' && result.code !== 0) {
    throw new Error(result.message || '失效操作失败')
  }

  return result.data || { success: true }
}
