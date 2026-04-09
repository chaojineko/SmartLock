Component({
  properties: {
    loading: {
      type: Boolean,
      value: false,
    },
    empty: {
      type: Boolean,
      value: false,
    },
    loadingText: {
      type: String,
      value: '加载中...',
    },
    emptyText: {
      type: String,
      value: '暂无数据',
    },
  },
})
