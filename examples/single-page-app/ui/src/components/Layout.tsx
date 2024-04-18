import {Flex} from '@chakra-ui/react'
import {ILayoutProps} from "../@types/app"
import {Header} from "./Header"

export default function Layout (props: ILayoutProps) {
  return (
    <Flex
      direction="column"
      align="center"
      maxW={{xl: "1200px"}}
      m="0 auto"
      {...props}>
      <Header />
      {props.children}
    </Flex>
  )
}
