import {IAuthAction, IAuthState, IDataAction, IDataState} from "../../@types/app"

export const userInitialState: IAuthState = {
  authenticating: false,
  failed: false,
  isLoggedIn: JSON.parse(localStorage.getItem("isLoggedIn") || 'false'),
  user: JSON.parse(localStorage.getItem("user") || 'null'),
  proxy_url: `${import.meta.env.VITE_APP_API_URL}/hub`,
  provider: import.meta.env.VITE_APP_AUTH_PROVIDER
}

export const userReducer = (state: IAuthState, action: IAuthAction): IAuthState => {
  switch (action.type) {
    case "AUTH": {
      return {
        ...state,
        authenticating: true
      }
    }
    case "ERROR": {
      return {
        ...state,
        authenticating: false,
        failed: true
      }
    }
    case "LOGIN": {
      if (!action.payload) {
        throw new Error('LOGIN called without payload')
      }
      localStorage.setItem("isLoggedIn", JSON.stringify(action.payload.isLoggedIn))
      localStorage.setItem("user", JSON.stringify(action.payload.user))
      return {
        ...state,
        authenticating: false,
        isLoggedIn: action.payload.isLoggedIn,
        user: action.payload.user
      }
    }
    case "LOGOUT": {
      localStorage.clear()
      return {
        ...state,
        authenticating: false,
        isLoggedIn: false,
        user: null
      }
    }
    case "RESET": {
      return {
        ...state,
        failed: false
      }
    }
    default:
      return state
  }
}

export const dataInitialState: IDataState = {
  data: JSON.parse(localStorage.getItem("data") || 'null'),
}

export const dataReducer = (state: IDataState, action: IDataAction): IDataState => {
  switch (action.type) {
    case "UPDATE": {
      if (!action.payload) {
        throw new Error('UPDATE called without payload')
      }
      const data = {
        ...JSON.parse(localStorage.getItem("data") || '{}'),
        ...action.payload}
      localStorage.setItem("data", JSON.stringify(data))
      return {
        ...state,
        data
      }
    }
    default:
      return state
  }
}
