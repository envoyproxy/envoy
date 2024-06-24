import {useToast} from '@chakra-ui/react'
import {useEffect, useContext} from "react"
import {Navigate} from "react-router-dom"
import {AuthContext} from "../App"
import {TAuthContext} from "../@types/app"
import Home from "./Home"

/*
Note: Envoy's oAuth implementation requires that a page be requested *after*
 a successful authorization/authentication.

 The consequence is that 2 pages are required to complete authentication -
 this one and Auth.tsx which does a hard redirect here.

*/

export default function Login() {
  const {state, dispatch} = useContext(AuthContext) as TAuthContext
  const {isLoggedIn} = state
  const toast = useToast()
  useEffect(() => {
    const {authenticating, failed, isLoggedIn, proxy_url} = state
    const fetchUser = async () => {
      dispatch({type: "AUTH"})
      const response = await fetch(`${proxy_url}/user`)
      const user = await response.json()
      dispatch({
        type: "LOGIN",
        payload: {user, isLoggedIn: true}
      })
    }
    if (!isLoggedIn && !authenticating && !failed) {
      fetchUser().catch(error => {
        dispatch({type: "ERROR"})
        toast({
          title: "Login failed.",
          description: `${error.message}`,
          status: "error",
          duration: 9000,
          isClosable: true,
        })
      })
    }
  }, [state, dispatch, toast])
  if (isLoggedIn) {
    return <Navigate to="/" />
  }
  return <Home />
}
