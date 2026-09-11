package shared

import "testing"

func TestAttributeIDOrdering(t *testing.T) {
	t.Parallel()

	tests := []struct {
		id   AttributeID
		want AttributeID
	}{
		{AttributeIDHealthCheck, 67},
		{AttributeIDUpstreamRequestedServerName, 68},
	}
	for _, test := range tests {
		if test.id != test.want {
			t.Errorf("attribute ID = %d, want %d", test.id, test.want)
		}
	}
}
