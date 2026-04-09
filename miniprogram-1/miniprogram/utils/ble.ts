const openAdapter = (): Promise<void> => {
  return new Promise((resolve, reject) => {
    wx.openBluetoothAdapter({
      success: () => resolve(),
      fail: (err) => reject(new Error(err.errMsg || '蓝牙适配器不可用')),
    })
  })
}

const LOCK_DEVICE_NAME = 'ESP32_LOCK'

const isLockDevice = (device: WechatMiniprogram.BlueToothDevice): boolean => {
  const name = (device.name || device.localName || '').trim()
  if (!name) {
    return false
  }
  const upper = name.toUpperCase()
  return upper.includes(LOCK_DEVICE_NAME) || (upper.includes('ESP32') && upper.includes('LOCK'))
}

export interface BleWriteTarget {
  serviceId: string
  characteristicId: string
}

const writeTargetCache = new Map<string, BleWriteTarget>()

const toArrayBuffer = (text: string): ArrayBuffer => {
  const buffer = new ArrayBuffer(text.length)
  const bytes = new Uint8Array(buffer)
  for (let i = 0; i < text.length; i += 1) {
    bytes[i] = text.charCodeAt(i) & 0xff
  }
  return buffer
}

export const discoverOneBleDevice = (): Promise<WechatMiniprogram.BlueToothDevice> => {
  return new Promise(async (resolve, reject) => {
    try {
      await openAdapter()
    } catch (err) {
      reject(err)
      return
    }

    const discoveredNames = new Set<string>()

    const timer = setTimeout(() => {
      wx.offBluetoothDeviceFound(onDeviceFound)
      wx.stopBluetoothDevicesDiscovery({})
      const hint = Array.from(discoveredNames).slice(0, 6).join(', ')
      reject(new Error(hint ? `未发现门锁蓝牙设备（已扫描: ${hint}）` : '未发现门锁蓝牙设备 ESP32_LOCK'))
    }, 8000)

    const onDeviceFound = (result: WechatMiniprogram.OnBluetoothDeviceFoundCallbackResult) => {
      result.devices.forEach((item) => {
        const showName = (item.name || item.localName || '').trim()
        if (showName) {
          discoveredNames.add(showName)
        }
      })

      const matched = result.devices.find((item) => item.deviceId && isLockDevice(item))
      if (!matched) {
        return
      }

      clearTimeout(timer)
      wx.offBluetoothDeviceFound(onDeviceFound)
      wx.stopBluetoothDevicesDiscovery({})
      resolve(matched)
    }

    wx.onBluetoothDeviceFound(onDeviceFound)

    wx.startBluetoothDevicesDiscovery({
      allowDuplicatesKey: false,
      success: () => undefined,
      fail: (err) => {
        clearTimeout(timer)
        wx.offBluetoothDeviceFound(onDeviceFound)
        reject(new Error(err.errMsg || '蓝牙扫描失败'))
      },
    })
  })
}

export const createConnection = (deviceId: string): Promise<void> => {
  return new Promise((resolve, reject) => {
    wx.createBLEConnection({
      deviceId,
      timeout: 8000,
      success: () => resolve(),
      fail: (err) => reject(new Error(err.errMsg || '连接蓝牙设备失败')),
    })
  })
}

export const closeConnection = (deviceId: string): Promise<void> => {
  return new Promise((resolve, reject) => {
    wx.closeBLEConnection({
      deviceId,
      success: () => {
        writeTargetCache.delete(deviceId)
        resolve()
      },
      fail: (err) => reject(new Error(err.errMsg || '断开蓝牙设备失败')),
    })
  })
}

const getServices = (deviceId: string): Promise<WechatMiniprogram.BLEService[]> => {
  return new Promise((resolve, reject) => {
    wx.getBLEDeviceServices({
      deviceId,
      success: (res) => resolve(res.services || []),
      fail: (err) => reject(new Error(err.errMsg || '读取蓝牙服务失败')),
    })
  })
}

const getCharacteristics = (
  deviceId: string,
  serviceId: string,
): Promise<WechatMiniprogram.BLECharacteristic[]> => {
  return new Promise((resolve, reject) => {
    wx.getBLEDeviceCharacteristics({
      deviceId,
      serviceId,
      success: (res) => resolve(res.characteristics || []),
      fail: (err) => reject(new Error(err.errMsg || '读取蓝牙特征失败')),
    })
  })
}

export const getWritableTarget = async (deviceId: string): Promise<BleWriteTarget> => {
  const cached = writeTargetCache.get(deviceId)
  if (cached) {
    return cached
  }

  const services = await getServices(deviceId)
  for (const service of services) {
    const chars = await getCharacteristics(deviceId, service.uuid)
    const writable = chars.find((item) => {
      const props = item.properties as WechatMiniprogram.BLECharacteristicProperties & {
        writeNoResponse?: boolean
      }
      return Boolean(props.write || props.writeNoResponse)
    })
    if (writable) {
      const target: BleWriteTarget = {
        serviceId: service.uuid,
        characteristicId: writable.uuid,
      }
      writeTargetCache.set(deviceId, target)
      return target
    }
  }

  throw new Error('未找到可写入的蓝牙特征')
}

export const writeBleCommand = async (deviceId: string, command: string): Promise<void> => {
  const target = await getWritableTarget(deviceId)
  const value = toArrayBuffer(command)

  await new Promise<void>((resolve, reject) => {
    wx.writeBLECharacteristicValue({
      deviceId,
      serviceId: target.serviceId,
      characteristicId: target.characteristicId,
      value,
      success: () => resolve(),
      fail: (err) => reject(new Error(err.errMsg || '蓝牙写入失败')),
    })
  })
}

export const writeBleUnlock = (deviceId: string): Promise<void> => {
  return writeBleCommand(deviceId, 'UNLOCK\r\n')
}
