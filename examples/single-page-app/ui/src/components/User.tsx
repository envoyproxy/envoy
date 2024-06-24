import {useContext} from "react"
import {ChevronDownIcon} from '@chakra-ui/icons'
import {
  Button,
  Flex,
  Image,
  Menu,
  MenuButton,
  MenuList,
  MenuItem,
  Modal,
  ModalBody,
  ModalCloseButton,
  ModalContent,
  ModalFooter,
  ModalHeader,
  ModalOverlay,
  Table,
  TableCaption,
  TableContainer,
  Tbody,
  Td,
  Thead,
  Th,
  Tr,
  Text,
  useDisclosure,
  useToast,
} from '@chakra-ui/react'
import {AuthContext} from "../App"
import {IUser, TAuthContext} from "../@types/app"
import {AuthProviders} from "../providers.tsx"

export const UserMenu = () => {
  const {state, dispatch} = useContext(AuthContext) as TAuthContext
  const {isOpen, onOpen, onClose} = useDisclosure()
  const {authenticating, isLoggedIn, provider, user} = state
  const authProvider = AuthProviders[provider]
  const toast = useToast()
  const handleLogin = async () => {
    // This is intercepted and redirected by Envoy
    window.location.href = '/login'
  }
  const handleLogout = async () => {
    const response = await fetch('/logout')
    if (response.status === 200) {
      await dispatch({
        type: "LOGOUT",
      })
    } else {
      toast({
        title: "Logout failed.",
        description: `${response.statusText}`,
        status: "error",
        duration: 9000,
        isClosable: true,
      })
    }
  }
  const {
    avatar_url = '...',
    login = '...',
    public_repos = '0',
    followers = '0',
    following = '0'} = user as IUser || {}
  if (!isLoggedIn) {
    const {icon: Icon, name: providerName} = authProvider
    const loginText = authenticating ? 'Logging in' : `Login to ${providerName}`;
    return (
      <Menu>
        <MenuButton as={Button} onClick={() => handleLogin()}>
          <Flex align="center">
            <Icon />
            <Text>{loginText}</Text>
          </Flex>
        </MenuButton>
      </Menu>)
  }
  return (
    <Menu>
      <MenuButton as={Button} rightIcon={<ChevronDownIcon />}>
        <Flex align="center">
          <Image
            boxSize='2rem'
            borderRadius='full'
            src={avatar_url}
            alt="Avatar"
            mr='12px'
          />
          <Text>{login}</Text>
        </Flex>
      </MenuButton>
      <MenuList>
        <MenuItem onClick={onOpen}>Info</MenuItem>
        <Modal
          isCentered
          onClose={onClose}
          isOpen={isOpen}
          motionPreset='slideInBottom'>
          <ModalOverlay />
          <ModalContent>
            <ModalHeader>
              <Flex align="center">
                <Image
                  boxSize='2rem'
                  borderRadius='full'
                  src={avatar_url}
                  alt="Avatar"
                  mr='12px'
                />
                <Text>{login}</Text>
              </Flex>
            </ModalHeader>
            <ModalCloseButton />
            <ModalBody>
              <TableContainer>
                <Table variant='simple'>
                  <TableCaption>Github user metrics</TableCaption>
                  <Thead>
                    <Tr>
                      <Th>metric</Th>
                      <Th isNumeric>count</Th>
                    </Tr>
                  </Thead>
                  <Tbody>
                    <Tr>
                      <Td>Repos</Td>
                      <Td isNumeric>{public_repos}</Td>
                    </Tr>
                    <Tr>
                      <Td>Followers</Td>
                      <Td isNumeric>{followers}</Td>
                    </Tr>
                    <Tr>
                      <Td>Following</Td>
                      <Td isNumeric>{following}</Td>
                    </Tr>
                  </Tbody>
                </Table>
              </TableContainer>
            </ModalBody>
            <ModalFooter>
              <Button mr={3} onClick={onClose}>
                Close
              </Button>
            </ModalFooter>
          </ModalContent>
        </Modal>
        <MenuItem onClick={()=> handleLogout()}>Logout</MenuItem>
      </MenuList>
    </Menu>)
}
