const cloud = require('wx-server-sdk')

cloud.init({ env: cloud.DYNAMIC_CURRENT_ENV })
const db = cloud.database()

exports.main = async (event) => {
  const wxContext = cloud.getWXContext()
  const grantId = (event.grantId || '').toString().trim()

  if (!grantId) {
    return {
      code: 400,
      message: 'grantId 不能为空',
    }
  }

  const docRes = await db.collection('grant_tickets').doc(grantId).get()
  const doc = docRes.data
  if (!doc) {
    return {
      code: 404,
      message: '未找到授权记录',
    }
  }

  const owner = doc.ownerOpenId || doc.openid || doc._openid || ''
  if (owner && owner !== wxContext.OPENID) {
    return {
      code: 403,
      message: '无权限失效该记录',
    }
  }

  if (doc.status !== 'active') {
    return {
      code: 409,
      message: '该二维码不是可失效状态',
    }
  }

  const now = Date.now()
  await db.collection('grant_tickets').doc(grantId).update({
    data: {
      status: 'revoked',
      revokedAt: now,
    },
  })

  return {
    code: 0,
    message: 'ok',
    data: {
      success: true,
    },
  }
}
