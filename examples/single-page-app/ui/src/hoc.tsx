import React, {useContext} from "react"

import {AuthContext, DataContext} from "./App"
import {
  IComponentWithDataProp,
  IComponentWithUserProp,
  TAuthContext,
  TDataContext} from "./@types/app"


function getDisplayName<T>(WrappedComponent: React.ComponentType<T>): string {
  return WrappedComponent.displayName || WrappedComponent.name || 'Component'
}

export function withAuth<T extends IComponentWithUserProp>(WrappedComponent: React.ComponentType<Omit<T, 'user'>>) {
  function WithAuth<TProps extends Omit<T, 'user'>>(props: TProps) {
    return (
      <WrappedComponent
        {...(props as TProps)}
        user={useContext(AuthContext) as TAuthContext} />)
  }
  WithAuth.displayName = `WithAuth(${getDisplayName(WrappedComponent)})`
  return WithAuth
}

export function withData<T extends IComponentWithDataProp>(WrappedComponent: React.ComponentType<Omit<T, 'data'>>) {
  function WithData<TProps extends Omit<T, 'data'>>(props: TProps) {
    return (
      <WrappedComponent
        {...(props as TProps)}
        data={useContext(DataContext) as TDataContext} />)
  }
  WithData.displayName = `WithData(${getDisplayName(WrappedComponent)})`
  return WithData
}
