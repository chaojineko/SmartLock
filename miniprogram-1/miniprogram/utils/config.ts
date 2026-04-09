export const APP_NAME = '小程序骨架'

export const CLOUD_ENV_ID = 'cloud1-8gfayfnzdb935815'
export const DEFAULT_LOCK_ID = 'LOCK001'
export const DEFAULT_DURATION_MIN = 30

export const ENV = {
	dev: {
		baseUrl: 'https://dev.example.com/api',
	},
	prod: {
		baseUrl: 'https://prod.example.com/api',
	},
}

export const DEFAULT_ENV: keyof typeof ENV = 'dev'
