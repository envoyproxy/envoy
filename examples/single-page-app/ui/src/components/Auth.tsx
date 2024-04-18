import {useEffect, useContext} from "react"
import {Navigate} from "react-router-dom"
import {AuthContext} from "../App"
import {TAuthContext} from "../@types/app"
import Home from "./Home"

export default function Auth() {
  const {dispatch, state} = useContext(AuthContext) as TAuthContext
  useEffect(() => {
    if (!state.isLoggedIn) {
      // We cannot login here as Envoy requires a request be made after
      // succesful authentication in order to trigger the completion of the
      // OAuth flow
      dispatch({type: "RESET"})
      window.location.href = '/login'
    }
  }, [state, dispatch])
  if (state.isLoggedIn) {
    return <Navigate to="/" />
  }
  return <Home />
}
