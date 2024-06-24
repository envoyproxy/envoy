
import {ReactNode} from 'react'

// user

export interface IAuthPayload {
  isLoggedIn: boolean
  user: IUser
}

export interface IAuthAction {
  email?: string
  payload?: IAuthPayload
  type: string
}

export interface IUserLogin {
  avatar_url: string
  html_url: string
  login: string
  name: string
}

export interface IUser {
  avatar_url: string
  email: string
  followers: number
  following: number
  login: string
  public_repos: string[]
}

export interface IAuthProvider {
  name: string
  icon: T
}

export interface IAuthProviders {
  [key: string]: IAuthProvider
}

export interface IAuthState {
  authenticating: boolean
  failed: boolean
  isLoggedIn: boolean
  user: IUser | null
  provider: string
  proxy_url: string
}

export type TAuthContext = {
  state: IAuthState
  dispatch: React.Dispatch<IAuthAction>
}

// data

export interface IRepo {
  html_url: string
}

export interface IFollower {
  avatar_url: string
  html_url: string
}

export interface IFollowing {
  avatar_url: string
  html_url: string
}

export interface IDataPayload {
  [key: string]: IRepo[] | IFollower[] | IFollowing[] | undefined
  repos?: IRepo[]
  followers?: IFollower[]
  following?: IFollowing[]
}

export interface IDataAction {
  payload?: IDataPayload
  type: string
}

export interface IData {
  repos?: IRepoInfo[]
  followers?: IUserLogin[]
  following?: IUserLogin[]
}

export interface IDataState {
  data: IData
}

export type TDataContext = {
  state: IDataState
  dispatch: React.Dispatch<IDataAction>
}

// components

interface IHeaderProps {
}

interface IHomeProps {
  isLoading?: boolean
}

interface ILayoutProps {
  children?: ReactNode
}

interface IActionState {
  isLoading: boolean
  errorMessage: string
}

interface IComponentWithUserProp {
  user: TAuthContext
}

interface IComponentWithDataProp {
  data: TDataContext
}

interface ITableResourceProps<T> extends IComponentWithUserProp, IComponentWithDataProp {
  headers: React.ComponentType
  name: "repos" | "followers" | "following"
  row: React.ComponentType<{resource: T}>
  title: string
}

interface IRepoInfo {
  html_url: string
  full_name: string
  updated_at: string
}
