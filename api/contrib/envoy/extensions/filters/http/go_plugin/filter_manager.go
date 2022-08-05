package plugin

//****************** filter factory start ******************//
// StreamFilterFactory is used to inherit from callbacks
// and generate a specific filter.
type StreamFilterFactory func(callbacks FilterCallbackHandler) StreamFilter

// StreamFilterConfigFactory stream filter config factory interface
type StreamFilterConfigFactory interface {
	// CreateFilterConfigFactory is create filter factory and it usually
	// occurs during the filter config parse phase.
	// config[] is google.protobuf.Any type and go plugin filter
	// can parse the detailed configuration according to its own pb.
	CreateFilterConfigFactory(config [][]byte) (StreamFilterFactory, error)
}

// StreamFilterConfigFactoryManager is manager stream filter config factory.
type StreamFilterConfigFactoryManager interface {
	// RegisterFilterConfigFactory register a config factory via filter name.
	RegisterFilterConfigFactory(name string, factory StreamFilterConfigFactory) StreamFilterConfigFactory
	// GetConfigFactory get a stream factory by filter name.
	GetFilterConfigFactory(name string) StreamFilterConfigFactory
}

// StreamFilterFactoryManager is manager stream filter factory.
type StreamFilterFactoryManager interface {
	// StoreFilterFactory is store a stream  filter factor by uuid
	// and each stream's filter mapped by a uuid at parse filter config phase.
	// The uuid is created during filter config parse phase.
	StoreFilterFactory(uuid uint64, ins StreamFilterFactory)
	// GetFilterFactory get a stream filter factor by uuid.
	GetFilterFactory(uuid uint64) StreamFilterFactory
	// DeleteFilterFactory remove a stream filter factor by uuid
	// and it usually occurs during the configuration update phase.
	DeleteFilterFactory(uuid uint64)
}

//****************** filter factory end ******************//

//***************** filter manager start *****************//
// StreamFilterManager is manager stream filter.
type StreamFilterManager interface {
	// StoreFilter is store a filter by uuid and it usually occurs during the
	// every new stream filter constructor phase.
	// The uuid is created during filter constructor phase.
	StoreFilter(uuid uint64, filter StreamFilter)
	// GetFilter is get a filter by uuid and use to call the filter's decodeHeader
	// decodeData etc.
	GetFilter(uuid uint64) StreamFilter
	// DeleteFilter remove a stream filter by uuid and it  usually occurs during
	// the stream filter destructor phase.
	DeleteFilter(uuid uint64)
}

//***************** filter manager end  *****************//
