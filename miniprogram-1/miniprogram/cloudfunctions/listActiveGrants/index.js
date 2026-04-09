const cloud = require('wx-server-sdk')

cloud.init({ env: cloud.DYNAMIC_CURRENT_ENV })
const db = cloud.database()
const _ = db.command

exports.main = async () => {
  const wxContext = cloud.getWXContext()
  const now = Date.now()

  const queryRes = await db
    .collection('grant_tickets')
    .where({
      status: 'active',
      expireAt: _.gt(now),
    })
    .orderBy('expireAt', 'asc')
    .limit(50)
    .get()

  const data = (queryRes.data || [])
    .filter((item) => {
      const owner = item.ownerOpenId || item.openid || item._openid || ''
      if (!owner) {
        // Compatibility mode for historical records without owner fields.
        return true
      }
      return owner === wxContext.OPENID
    })
    .map((item) => ({
    grantId: item._id,
    lockId: item.lockId,
    grantToken: item.grantToken,
    validFrom: item.validFrom,
    expireAt: item.expireAt,
    qrDataUrl: item.qrDataUrl,
  }))

  return {
    code: 0,
    message: 'ok',
    data,
  }
}
