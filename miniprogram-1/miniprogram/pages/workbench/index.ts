import { getWorkbenchItems } from '../../services/index'
import type { CommonItem } from '../../utils/types'
import {
  closeConnection,
  createConnection,
  discoverOneBleDevice,
  writeBleUnlock,
} from '../../utils/ble'

interface WorkbenchData {
  loading: boolean
  items: CommonItem[]
  bleBusy: boolean
  bleConnected: boolean
  bleDeviceName: string
  bleStatus: string
  bleDeviceId: string
}

Page<WorkbenchData>({
  data: {
    loading: false,
    items: [],
    bleBusy: false,
    bleConnected: false,
    bleDeviceName: '',
    bleStatus: '未连接蓝牙门锁',
    bleDeviceId: '',
  },

  onShow() {
    this.loadItems()
  },

  async onPullDownRefresh() {
    await this.loadItems()
    wx.stopPullDownRefresh()
  },

  async loadItems() {
    this.setData({ loading: true })
    try {
      const items = await getWorkbenchItems()
      this.setData({ items })
    } finally {
      this.setData({ loading: false })
    }
  },

  goGrantPage() {
    wx.navigateTo({
      url: '/pages/grant/index',
    })
  },

  goActiveGrantPage() {
    wx.navigateTo({
      url: '/pages/grant-active/index',
    })
  },

  async onUnload() {
    const { bleConnected, bleDeviceId } = this.data
    if (bleConnected && bleDeviceId) {
      try {
        await closeConnection(bleDeviceId)
      } catch (err) {
        console.warn('关闭蓝牙连接失败', err)
      }
    }
  },

  async handleBleUnlockTap() {
    if (this.data.bleBusy) {
      return
    }

    this.setData({ bleBusy: true })
    try {
      let deviceId = this.data.bleDeviceId
      let deviceName = this.data.bleDeviceName

      if (!this.data.bleConnected || !deviceId) {
        this.setData({ bleStatus: '扫描蓝牙设备中...' })
        const device = await discoverOneBleDevice()
        deviceId = device.deviceId
        deviceName = device.name || device.localName || '未命名设备'

        this.setData({ bleStatus: `连接中: ${deviceName}` })
        await createConnection(deviceId)
        this.setData({
          bleConnected: true,
          bleDeviceId: deviceId,
          bleDeviceName: deviceName,
        })
      }

      this.setData({ bleStatus: '发送解锁指令...' })
      await writeBleUnlock(deviceId)
      this.setData({ bleStatus: `已发送解锁: ${deviceName || '门锁设备'}` })
      wx.showToast({
        title: '蓝牙解锁指令已发送',
        icon: 'success',
      })
    } catch (err) {
      const error = err as Error
      this.setData({ bleStatus: `解锁失败: ${error.message || '未知错误'}` })
      wx.showToast({
        title: '蓝牙解锁失败',
        icon: 'none',
      })
    } finally {
      this.setData({ bleBusy: false })
    }
  },
})
