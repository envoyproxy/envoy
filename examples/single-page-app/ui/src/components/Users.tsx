import {
  Flex,
  Image,
  Link,
  Td,
  Th,
} from '@chakra-ui/react'
import React from "react"

import {
  IRepoInfo,
  IUserLogin} from "../@types/app"


export const RelatedUserTableHeaders = () => {
  return (
    <>
      <Th>User</Th>
      <Th>Full name</Th>
    </>)
}

export const RelatedUserTr: React.FC<{resource: IRepoInfo | IUserLogin}> = ({resource}) => {
  const user = resource as IUserLogin
  return (
    <>
      <Td>
        <Flex align="center">
          <Image
            boxSize='2rem'
            borderRadius='full'
            alt="Avatar"
            mr='12px'
            src={user.avatar_url} />
          <Link href={user.html_url}>
            {user.login}
          </Link>
        </Flex>
      </Td>
      <Td>
        {user.name}
      </Td>
    </>)
}
