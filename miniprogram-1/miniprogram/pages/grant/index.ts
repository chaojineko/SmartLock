import { createGrant } from '../../services/index'
import { DEFAULT_DURATION_MIN, DEFAULT_LOCK_ID } from '../../utils/config'
import type { CreateGrantResult, GrantTimeMode } from '../../utils/types'

let countdownTimer: number | null = null

const pad2 = (value: number) => value.toString().padStart(2, '0')

const formatDatePart = (date: Date) => {
  return `${date.getFullYear()}-${pad2(date.getMonth() + 1)}-${pad2(date.getDate())}`
}

const formatTimePart = (date: Date) => {
  return `${pad2(date.getHours())}:${pad2(date.getMinutes())}`
}

const parseDateTime = (dateText: string, timeText: string) => {
  const [year, month, day] = dateText.split('-').map(Number)
  const [hour, minute] = timeText.split(':').map(Number)
  return new Date(year, month - 1, day, hour, minute, 0, 0).getTime()
}

const buildDefaultWindow = () => {
  const now = new Date()
  const start = new Date(now.getTime() + 60 * 1000)
  const end = new Date(start.getTime() + 60 * 60 * 1000)

  return {
    startDate: formatDatePart(start),
    startTime: formatTimePart(start),
    endDate: formatDatePart(end),
    endTime: formatTimePart(end),
  }
}

const defaultWindow = buildDefaultWindow()

const saveBase64Image = (dataUrl: string) => {
  return new Promise<string>((resolve, reject) => {
    const base64 = dataUrl.replace(/^data:image\/[a-zA-Z0-9+.-]+;base64,/, '')
    if (!base64) {
      reject(new Error('二维码图片数据无效'))
      return
    }

    const filePath = `${wx.env.USER_DATA_PATH}/grant-qr-${Date.now()}.png`
    const fs = wx.getFileSystemManager()

    fs.writeFile({
      filePath,
      data: base64,
      encoding: 'base64',
      success: () => resolve(filePath),
      fail: (err) => {
        const message = err && err.errMsg ? err.errMsg : '二维码图片写入失败'
        reject(new Error(message))
      },
    })
  })
}

const resolveQrImagePath = async (input?: string) => {
  if (!input) {
    return ''
  }

  if (
    input.startsWith('wxfile://') ||
    input.startsWith('cloud://') ||
    input.startsWith('http://') ||
    input.startsWith('https://')
  ) {
    return input
  }

  if (input.startsWith('data:image')) {
    return saveBase64Image(input)
  }

  return ''
}

const toExpireAtMs = (expireAt: number | string) => {
  if (typeof expireAt === 'number') {
    return expireAt < 1000000000000 ? expireAt * 1000 : expireAt
  }

  const parsed = Date.parse(expireAt)
  if (Number.isNaN(parsed)) {
    return Date.now()
  }
  return parsed
}

Page({
  data: {
    startDate: defaultWindow.startDate,
    startTime: defaultWindow.startTime,
    endDate: defaultWindow.endDate,
    endTime: defaultWindow.endTime,
    lockId: DEFAULT_LOCK_ID,
    timeMode: 'duration',
    durationMinInput: String(DEFAULT_DURATION_MIN),
    loading: false,
    grantToken: '',
    validFrom: 0,
    expireAt: 0,
    remainingSec: 0,
    statusText: '',
    qrImagePath: '',
    qrSourceType: '',
    tips: '',
  },

  onHide() {
    this.clearCountdown()
  },

  onUnload() {
    this.clearCountdown()
  },

  onLockIdInput(e: WechatMiniprogram.Input) {
    this.setData({ lockId: e.detail.value.trim() })
  },

  onModeChange(e: WechatMiniprogram.RadioGroupChange) {
    const value = e.detail.value as GrantTimeMode
    this.setData({ timeMode: value })
  },

  onDurationInput(e: WechatMiniprogram.Input) {
    this.setData({ durationMinInput: e.detail.value.trim() })
  },

  onStartDateChange(e: WechatMiniprogram.PickerChange) {
    this.setData({ startDate: String(e.detail.value) })
  },

  onStartTimeChange(e: WechatMiniprogram.PickerChange) {
    this.setData({ startTime: String(e.detail.value) })
  },

  onEndDateChange(e: WechatMiniprogram.PickerChange) {
    this.setData({ endDate: String(e.detail.value) })
  },

  onEndTimeChange(e: WechatMiniprogram.PickerChange) {
    this.setData({ endTime: String(e.detail.value) })
  },

  async createGrant() {
    if (this.data.loading) {
      return
    }

    if (!this.data.lockId) {
      wx.showToast({ title: '请填写门锁ID', icon: 'none' })
      return
    }

    let payload: {
      lockId: string
      mode: GrantTimeMode
      durationMin?: number
      startAt?: number
      endAt?: number
    }

    if (this.data.timeMode === 'duration') {
      const durationMin = Number(this.data.durationMinInput)
      if (!Number.isInteger(durationMin) || durationMin < 1 || durationMin > 1440) {
        wx.showToast({ title: '时长请输入1-1440分钟', icon: 'none' })
        return
      }

      payload = {
        lockId: this.data.lockId,
        mode: 'duration',
        durationMin,
      }
    } else {
      const startAt = parseDateTime(this.data.startDate, this.data.startTime)
      const endAt = parseDateTime(this.data.endDate, this.data.endTime)
      const now = Date.now()

      if (!Number.isFinite(startAt) || !Number.isFinite(endAt)) {
        wx.showToast({ title: '时间格式无效', icon: 'none' })
        return
      }

      if (startAt < now - 60 * 1000) {
        wx.showToast({ title: '开始时间不能早于当前时间', icon: 'none' })
        return
      }

      if (endAt <= startAt) {
        wx.showToast({ title: '结束时间必须晚于开始时间', icon: 'none' })
        return
      }

      const maxDuration = 7 * 24 * 60 * 60 * 1000
      if (endAt - startAt > maxDuration) {
        wx.showToast({ title: '时间窗口不能超过7天', icon: 'none' })
        return
      }

      payload = {
        lockId: this.data.lockId,
        mode: 'window',
        startAt,
        endAt,
      }
    }

    this.setData({ loading: true, tips: '' })

    try {
      const result = await createGrant(payload)

      await this.applyGrantResult(result)
      wx.showToast({ title: '二维码已生成', icon: 'success' })
    } catch (error) {
      const message = error instanceof Error ? error.message : '生成失败'
      wx.showToast({ title: message, icon: 'none' })
    } finally {
      this.setData({ loading: false })
    }
  },

  async applyGrantResult(result: CreateGrantResult) {
    const validFromMs = result.validFrom ? toExpireAtMs(result.validFrom) : Date.now()
    const expireAtMs = toExpireAtMs(result.expireAt)
    const qrImagePath = await resolveQrImagePath(result.qrDataUrl)
    const qrSourceType = result.qrDataUrl ? result.qrDataUrl.slice(0, 16) : 'none'
    const tips = qrImagePath
      ? ''
      : '当前仅显示了 token，请检查云函数是否返回 qrDataUrl'

    this.setData({
      grantToken: result.grantToken,
      validFrom: validFromMs,
      expireAt: expireAtMs,
      qrImagePath,
      qrSourceType,
      tips,
    })

    this.startCountdown(validFromMs, expireAtMs)
  },

  startCountdown(validFromMs: number, expireAtMs: number) {
    this.clearCountdown()

    const update = () => {
      const now = Date.now()
      if (now < validFromMs) {
        const startInSec = Math.max(0, Math.floor((validFromMs - now) / 1000))
        this.setData({
          statusText: `未生效，${Math.ceil(startInSec / 60)}分钟后开始`,
          remainingSec: startInSec,
        })
        return
      }

      const remaining = Math.max(0, Math.floor((expireAtMs - now) / 1000))
      this.setData({
        statusText: remaining > 0 ? '生效中' : '已过期',
        remainingSec: remaining,
      })

      if (remaining <= 0) {
        this.clearCountdown()
      }
    }

    update()
    countdownTimer = setInterval(update, 1000) as unknown as number
  },

  clearCountdown() {
    if (countdownTimer !== null) {
      clearInterval(countdownTimer)
      countdownTimer = null
    }
  },
})
