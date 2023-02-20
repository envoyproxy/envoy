package main

type pluginConfig struct {
	cluster string
}

type clusterSpecifier struct {
	config *pluginConfig
}

func (s *clusterSpecifier) Choose() string {
	return s.config.cluster
}
