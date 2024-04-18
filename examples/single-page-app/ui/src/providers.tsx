import GithubIcon from "mdi-react/GithubIcon"

import {IAuthProviders} from "./@types/app"
import {MyhubIcon} from "./myhub"


export const AuthProviders: IAuthProviders = {
  "myhub": {
    "name": "Myhub",
    "icon": MyhubIcon},
  "github": {
    "name": "Github",
    "icon": GithubIcon}}
