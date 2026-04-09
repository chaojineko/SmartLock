import type { CommonItem } from '../utils/types'

const wait = (ms: number) => new Promise<void>((resolve) => setTimeout(resolve, ms))

export const getWorkbenchItems = async (): Promise<CommonItem[]> => {
  await wait(400)

  return [
    { id: '1', title: '授权管理模块', createdAt: '2026-03-26 10:00:00' },
    { id: '2', title: '设备状态模块', createdAt: '2026-03-26 10:05:00' },
    { id: '3', title: '消息通知模块', createdAt: '2026-03-26 10:08:00' },
  ]
}
