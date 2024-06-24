import {
  Tab,
  TabList,
  TabPanel,
  TabPanels,
  Tabs,
  Text,
} from '@chakra-ui/react'
import {useContext} from "react"

import {AuthContext} from "../App"
import {
  IHomeProps,
  TAuthContext} from "../@types/app"
import Layout from "./Layout"
import {RepoTableHeaders, RepoTr} from "./Repos"
import {RelatedUserTableHeaders, RelatedUserTr} from "./Users"
import {Resources} from "./Resources"

export const Content = () => {
  const {state: userState} = useContext(AuthContext) as TAuthContext
  const {user} = userState
  if (!userState.isLoggedIn || !user) {
    return <Text>Login to query APIs</Text>
  }
  return (
    <Tabs>
      <TabList>
        <Tab>Repos</Tab>
        <Tab>Followers</Tab>
        <Tab>Following</Tab>
      </TabList>
      <TabPanels>
        <TabPanel>
          <Resources
            name="repos"
            title="Repositories"
            headers={RepoTableHeaders}
            row={RepoTr} />
        </TabPanel>
        <TabPanel>
          <Resources
            name="followers"
            title="Followers"
            headers={RelatedUserTableHeaders}
            row={RelatedUserTr} />
        </TabPanel>
        <TabPanel>
          <Resources
            name="following"
            title="Following"
            headers={RelatedUserTableHeaders}
            row={RelatedUserTr} />
        </TabPanel>
      </TabPanels>
    </Tabs>
  )
}

export default function Home (props: IHomeProps) {
  return (
    <Layout {...props}>
      <Content />
    </Layout>
  )
}
