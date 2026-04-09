const cloud = require('wx-server-sdk')
const QRCode = require('qrcode')

cloud.init({ env: cloud.DYNAMIC_CURRENT_ENV })
const db = cloud.database()

const randomToken = () => {
  return `${Math.random().toString(36).slice(2)}${Date.now().toString(36)}`
}

const clamp = (value, min, max) => Math.min(max, Math.max(min, value))

exports.main = async (event, context) => {
  const lockId = (event.lockId || 'LOCK001').toString().trim()
  const mode = event.mode === 'window' ? 'window' : 'duration'

  if (!lockId) {
    return {
      code: 400,
      message: 'lockId 不能为空',
    }
  }

  const wxContext = cloud.getWXContext()
  const now = Date.now()
  let validFrom = now
  let expireAt = now + 30 * 60 * 1000

  if (mode === 'window') {
    const startAt = Number(event.startAt)
    const endAt = Number(event.endAt)

    if (!Number.isFinite(startAt) || !Number.isFinite(endAt)) {
      return {
        code: 400,
        message: 'startAt/endAt 参数无效',
      }
    }

    if (endAt <= startAt) {
      return {
        code: 400,
        message: '结束时间必须晚于开始时间',
      }
    }

    const maxDuration = 7 * 24 * 60 * 60 * 1000
    if (endAt - startAt > maxDuration) {
      return {
        code: 400,
        message: '时间窗口不能超过7天',
      }
    }

    if (startAt < now - 60 * 1000) {
      return {
        code: 400,
        message: '开始时间不能早于当前时间',
      }
    }

    validFrom = startAt
    expireAt = endAt
  } else {
    const durationMinRaw = Number(event.durationMin || 30)
    const durationMin = Number.isFinite(durationMinRaw) ? clamp(Math.floor(durationMinRaw), 1, 1440) : 30
    validFrom = now
    expireAt = now + durationMin * 60 * 1000
  }

  const grantToken = randomToken()

  const payload = JSON.stringify({
    v: 1,
    t: grantToken,
    l: lockId,
    s: validFrom,
    e: expireAt,
  })

  const qrDataUrl = await QRCode.toDataURL(payload, {
    errorCorrectionLevel: 'M',
    width: 320,
    margin: 1,
  })

  console.log('createGrant qrDataUrl', {
    length: qrDataUrl ? qrDataUrl.length : 0,
    prefix: qrDataUrl ? qrDataUrl.slice(0, 24) : '',
  })

  const addRes = await db.collection('grant_tickets').add({
    data: {
      ownerOpenId: wxContext.OPENID,
      ownerAppId: wxContext.APPID,
      lockId,
      mode,
      grantToken,
      validFrom,
      expireAt,
      status: 'active',
      qrDataUrl,
      createdAt: now,
      revokedAt: null,
      usedAt: null,
    },
  })

  const grantId = addRes && addRes._id ? addRes._id : ''

  return {
    code: 0,
    message: 'ok',
    data: {
      grantId,
      grantToken,
      validFrom,
      expireAt,
      qrDataUrl,
      lockId,
      mode,
      openid: wxContext.OPENID,
    },
  }
}
