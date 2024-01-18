import {useState} from "react"
import {CloseIcon, ChevronUpIcon} from '@chakra-ui/icons'
import {Box, Flex} from '@chakra-ui/react'
import {IHeaderProps} from "../@types/app"
import {UserMenu} from "./User"

export const Header = (props: IHeaderProps) => {
  const [show, setShow] = useState(false)
  const toggleMenu = () => setShow(!show)
  return (
    <Flex
      as="nav"
      align="center"
      justify="space-between"
      wrap="wrap"
      w="100%"
      mb={8}
      p={8}
      {...props}>
      <Flex align="center">
      </Flex>
      <Box display={{base: "block", md: "none"}} onClick={toggleMenu}>
        {show ? <CloseIcon /> : <ChevronUpIcon />}
      </Box>
      <Box
        display={{base: show ? "block" : "none", md: "block"}}
        flexBasis={{base: "100%", md: "auto"}}>
        <Flex
          align={["center", "center", "center", "center"]}
          justify={["center", "space-between", "flex-end", "flex-end"]}
          direction={["column", "row", "row", "row"]}
          pt={[4, 4, 0, 0]}>
          <UserMenu />
        </Flex>
      </Box>
    </Flex>
  )
}
