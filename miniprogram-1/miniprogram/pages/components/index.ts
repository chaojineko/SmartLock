interface ComponentItem {
  name: string
  status: string
}

interface ComponentPageData {
  list: ComponentItem[]
}

Page<ComponentPageData>({
  data: {
    list: [
      { name: '基础按钮', status: '待接入' },
      { name: '信息卡片', status: '待接入' },
      { name: '空态组件', status: '待接入' },
    ],
  },
})
