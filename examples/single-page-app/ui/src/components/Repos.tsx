import {
  Flex,
  Link,
  Td,
  Th,
} from '@chakra-ui/react'
import React from "react"

import {
  IRepoInfo,
  IUserLogin} from "../@types/app"


export const RepoTableHeaders = () => {
  return (
    <>
      <Th>Repo</Th>
      <Th>Updated</Th>
    </>)
}

export const RepoTr: React.FC<{resource: IRepoInfo | IUserLogin}> = ({resource}) => {
  const repo = resource as IRepoInfo
  return (
    <>
      <Td>
        <Flex align="center">
          <Link href={repo.html_url}>
            {repo.full_name}
          </Link>
        </Flex>
      </Td>
      <Td>{repo.updated_at}</Td>
    </>)
}
