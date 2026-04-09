import { listActiveGrants, revokeGrant } from '../../services/index'
import type { ActiveGrantItem } from '../../utils/types'

interface GrantViewItem extends ActiveGrantItem {
  qrImagePath: string
  remainMin: number
  validRangeText: string
  stateText: string
}

const saveBase64Image = (dataUrl: string) => {
  return new Promise<string>((resolve, reject) => {
    const base64 = dataUrl.replace(/^data:image\/[a-zA-Z0-9+.-]+;base64,/, '')
    if (!base64) {
      reject(new Error('二维码图片数据无效'))
      return
    }

    const filePath = `${wx.env.USER_DATA_PATH}/active-qr-${Date.now()}-${Math.random().toString(36).slice(2)}.png`
    wx.getFileSystemManager().writeFile({
      filePath,
      data: base64,
      encoding: 'base64',
      success: () => resolve(filePath),
      fail: () => reject(new Error('二维码图片写入失败')),
    })
  })
}

const resolveQrImagePath = async (input?: string) => {
  if (!input) {
    return ''
  }

  if (input.startsWith('wxfile://') || input.startsWith('cloud://') || input.startsWith('http://') || input.startsWith('https://')) {
    return input
  }

  if (input.startsWith('data:image')) {
    return saveBase64Image(input)
  }

  return ''
}

const formatDateTime = (timestamp: number) => {
  const date = new Date(timestamp)
  const pad2 = (v: number) => v.toString().padStart(2, '0')
  return `${date.getMonth() + 1}-${date.getDate()} ${pad2(date.getHours())}:${pad2(date.getMinutes())}`
}

Page({
  data: {
    loading: false,
    list: [] as GrantViewItem[],
  },

  onShow() {
    this.loadList()
  },

  async onPullDownRefresh() {
    await this.loadList()
    wx.stopPullDownRefresh()
  },

  async loadList() {
    this.setData({ loading: true })
    try {
      const grants = await listActiveGrants()
      const now = Date.now()
      const viewList: GrantViewItem[] = []

      for (const item of grants) {
        const qrImagePath = await resolveQrImagePath(item.qrDataUrl)
        const remainMin = Math.max(0, Math.ceil((item.expireAt - now) / 60000))
        const stateText = item.validFrom > now ? '未生效' : '生效中'
        viewList.push({
          ...item,
          qrImagePath,
          remainMin,
          stateText,
          validRangeText: `${formatDateTime(item.validFrom)} ~ ${formatDateTime(item.expireAt)}`,
        })
      }

      this.setData({ list: viewList })
    } catch (error) {
      const message = error instanceof Error ? error.message : '加载失败'
      wx.showToast({ title: message, icon: 'none' })
    } finally {
      this.setData({ loading: false })
    }
  },

  onRevokeTap(e: WechatMiniprogram.BaseEvent) {
    const grantId = String(e.currentTarget.dataset.id || '')
    if (!grantId) {
      return
    }

    wx.showModal({
      title: '确认失效',
      content: '失效后该二维码将无法继续开锁，是否继续？',
      success: async (res) => {
        if (!res.confirm) {
          return
        }

        try {
          await revokeGrant(grantId)
          wx.showToast({ title: '已失效', icon: 'success' })
          await this.loadList()
        } catch (error) {
          const message = error instanceof Error ? error.message : '失效失败'
          wx.showToast({ title: message, icon: 'none' })
        }
      },
    })
  },
})
