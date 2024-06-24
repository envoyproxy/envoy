import {
  Button,
  Table,
  TableCaption,
  TableContainer,
  Tbody,
  Thead,
  Tr,
} from '@chakra-ui/react'
import React from "react"

import {withAuth, withData} from "../hoc"
import {
  IActionState,
  IDataPayload,
  IRepoInfo,
  ITableResourceProps,
  IUserLogin} from "../@types/app"

class BaseResources<T> extends React.Component<ITableResourceProps<T>, IActionState> {

  constructor(props: ITableResourceProps<T>) {
    super(props)
    this.state = {errorMessage: '', isLoading: false}
  }

  render() {
    const {data, name, headers: Headers, row: Row, title, user} = this.props
    const rows = data.state.data?.[name]
    const {user: userData} = user.state || {}
    const {login} = userData || {}

    if (!login) {
      return ''
    }

    if (!rows || !Array.isArray(rows)) {
      return <Button onClick={() => this.updateResources()}>Update {name}</Button>
    }

    return (
      <>
        <Button onClick={() => this.updateResources()}>Update {name}</Button>
        <TableContainer>
          <Table variant="simple">
            <TableCaption>{title}</TableCaption>
            <Thead>
              <Tr>
                <Headers />
              </Tr>
            </Thead>
            <Tbody>
              {rows.map((resource, index: number) => (
                <Tr key={index}>
                  <Row resource={resource as T} />
                </Tr>
              ))}
            </Tbody>
          </Table>
        </TableContainer>
      </>
    )
  }

  updateResources = async () => {
    const {data, name, user} = this.props
    const {dispatch} = data
    const {proxy_url, user: userData} = user.state
    if (!userData) {
      return
    }
    const {login} = userData
    try {
      const response = await fetch(`${proxy_url}/users/${login}/${name}`)
      const resources = await response.json()
      const payload: IDataPayload = {}
      payload[name] = resources
      dispatch({
        type: 'UPDATE',
        payload,
      })
    } catch (error) {
      const e = error as Record<'message', string>
      this.setState({
        isLoading: false,
        errorMessage: `Sorry! Fetching ${name} failed\n${e.message}`,
      })
    }
 }
}

const BaseResourcesWithProps = (props: ITableResourceProps<IRepoInfo | IUserLogin>) => {
  return <BaseResources {...props} />
}

export const Resources = withAuth(withData(BaseResourcesWithProps as React.ComponentType))
