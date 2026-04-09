const cloud = require('wx-server-sdk')

cloud.init({ env: cloud.DYNAMIC_CURRENT_ENV })
const db = cloud.database()
const _ = db.command

const parseFormBody = (text) => {
  const result = {}
  if (!text) {
    return result
  }

  text.split('&').forEach((part) => {
    const index = part.indexOf('=')
    if (index <= 0) {
      return
    }
    const key = decodeURIComponent(part.slice(0, index))
    const value = decodeURIComponent(part.slice(index + 1))
    result[key] = value
  })

  return result
}

const normalizeEvent = (event) => {
  if (!event || typeof event !== 'object') {
    return {}
  }

  let payload = event
  let bodyText = ''

  if (typeof event.body === 'string') {
    bodyText = event.body
  }

  if (event.isBase64Encoded && bodyText) {
    try {
      bodyText = Buffer.from(bodyText, 'base64').toString('utf8')
    } catch (error) {
      bodyText = event.body
    }
  }

  if (bodyText.trim()) {
    const trimmed = bodyText.trim()
    if (trimmed.startsWith('{')) {
      try {
        payload = JSON.parse(trimmed)
      } catch (error) {
        payload = event
      }
    } else {
      payload = parseFormBody(trimmed)
    }
  } else if (event.body && typeof event.body === 'object') {
    payload = event.body
  } else if (event.queryString && typeof event.queryString === 'object') {
    payload = event.queryString
  }

  return payload
}

exports.main = async (event) => {
  const payload = normalizeEvent(event)
  const grantToken = (payload.grantToken || '').toString().trim()
  const lockId = (payload.lockId || '').toString().trim()
  const now = Date.now()

  console.log('verifyGrant input', {
    grantToken,
    lockId,
    hasBody: !!event.body,
    isBase64Encoded: !!event.isBase64Encoded,
  })

  if (!grantToken) {
    return {
      code: 400,
      message: 'grantToken 不能为空',
      data: { valid: false, reason: 'empty_token' },
    }
  }

  if (!lockId) {
    return {
      code: 400,
      message: 'lockId 不能为空',
      data: { valid: false, reason: 'empty_lock' },
    }
  }

  const queryRes = await db
    .collection('grant_tickets')
    .where({
      grantToken,
      lockId,
    })
    .limit(1)
    .get()

  const item = queryRes.data && queryRes.data.length > 0 ? queryRes.data[0] : null
  if (!item) {
    return {
      code: 404,
      message: '授权不存在',
      data: { valid: false, reason: 'not_found' },
    }
  }

  if (item.status !== 'active') {
    return {
      code: 200,
      message: '授权已失效',
      data: { valid: false, reason: item.status || 'inactive' },
    }
  }

  if (Number(item.validFrom) > now) {
    return {
      code: 200,
      message: '授权未生效',
      data: { valid: false, reason: 'not_started', validFrom: item.validFrom, expireAt: item.expireAt },
    }
  }

  if (Number(item.expireAt) <= now) {
    await db.collection('grant_tickets').doc(item._id).update({
      data: {
        status: 'expired',
      },
    })

    return {
      code: 200,
      message: '授权已过期',
      data: { valid: false, reason: 'expired', validFrom: item.validFrom, expireAt: item.expireAt },
    }
  }

  const consumeRes = await db
    .collection('grant_tickets')
    .where({
      _id: item._id,
      status: 'active',
      validFrom: _.lte(now),
      expireAt: _.gt(now),
    })
    .update({
      data: {
        status: 'used',
        usedAt: now,
      },
    })

  if (!consumeRes.stats || consumeRes.stats.updated === 0) {
    return {
      code: 409,
      message: '授权状态冲突',
      data: { valid: false, reason: 'race_conflict' },
    }
  }

  return {
    code: 0,
    message: 'ok',
    data: {
      valid: true,
      reason: 'ok',
      grantId: item._id,
      lockId: item.lockId,
      validFrom: item.validFrom,
      expireAt: item.expireAt,
    },
  }
}
