#!/usr/bin/env python3
"""Tests for cve_scan."""

from collections import defaultdict
import datetime as dt
import unittest

import cve_scan


class CveScanTest(unittest.TestCase):

    def test_parse_cve_json(self):
        cve_json = {
            'CVE_Items': [
                {
                    'cve': {
                        'CVE_data_meta': {
                            'ID': 'CVE-2020-1234'
                        },
                        'description': {
                            'description_data': [{
                                'value': 'foo'
                            }]
                        }
                    },
                    'configurations': {
                        'nodes': [{
                            'cpe_match': [{
                                'cpe23Uri': 'cpe:2.3:a:foo:bar:1.2.3'
                            }],
                        }],
                    },
                    'impact': {
                        'baseMetricV3': {
                            'cvssV3': {
                                'baseScore': 3.4,
                                'baseSeverity': 'LOW'
                            }
                        }
                    },
                    'publishedDate': '2020-03-17T00:59Z',
                    'lastModifiedDate': '2020-04-17T00:59Z'
                },
                {
                    'cve': {
                        'CVE_data_meta': {
                            'ID': 'CVE-2020-1235'
                        },
                        'description': {
                            'description_data': [{
                                'value': 'bar'
                            }]
                        }
                    },
                    'configurations': {
                        'nodes': [{
                            'cpe_match': [{
                                'cpe23Uri': 'cpe:2.3:a:foo:bar:1.2.3'
                            }],
                            'children': [
                                {
                                    'cpe_match': [{
                                        'cpe23Uri': 'cpe:2.3:a:foo:baz:3.2.3'
                                    }]
                                },
                                {
                                    'cpe_match': [{
                                        'cpe23Uri': 'cpe:2.3:a:foo:*:*'
                                    }, {
                                        'cpe23Uri': 'cpe:2.3:a:wat:bar:1.2.3'
                                    }]
                                },
                            ],
                        }],
                    },
                    'impact': {
                        'baseMetricV3': {
                            'cvssV3': {
                                'baseScore': 9.9,
                                'baseSeverity': 'HIGH'
                            }
                        }
                    },
                    'publishedDate': '2020-03-18T00:59Z',
                    'lastModifiedDate': '2020-04-18T00:59Z'
                },
            ]
        }
        cves = {}
        cpe_revmap = defaultdict(set)
        cve_scan.parse_cve_json(cve_json, cves, cpe_revmap)
        self.maxDiff = None
        self.assertDictEqual(
            cves, {
                'CVE-2020-1234':
                    cve_scan.Cve(
                        id='CVE-2020-1234',
                        description='foo',
                        cpes=set([self.build_cpe('cpe:2.3:a:foo:bar:1.2.3')]),
                        score=3.4,
                        severity='LOW',
                        published_date=dt.date(2020, 3, 17),
                        last_modified_date=dt.date(2020, 4, 17)),
                'CVE-2020-1235':
                    cve_scan.Cve(
                        id='CVE-2020-1235',
                        description='bar',
                        cpes=set(
                            map(
                                self.build_cpe, [
                                    'cpe:2.3:a:foo:bar:1.2.3', 'cpe:2.3:a:foo:baz:3.2.3',
                                    'cpe:2.3:a:foo:*:*', 'cpe:2.3:a:wat:bar:1.2.3'
                                ])),
                        score=9.9,
                        severity='HIGH',
                        published_date=dt.date(2020, 3, 18),
                        last_modified_date=dt.date(2020, 4, 18))
            })
        self.assertDictEqual(
            cpe_revmap, {
                'cpe:2.3:a:foo:*:*': {'CVE-2020-1234', 'CVE-2020-1235'},
                'cpe:2.3:a:wat:*:*': {'CVE-2020-1235'}
            })

    def build_cpe(self, cpe_str):
        return cve_scan.Cpe.from_string(cpe_str)

    def build_dep(self, cpe_str, version=None, release_date=None):
        return {'cpe': cpe_str, 'version': version, 'release_date': release_date}

    def cpe_match(self, cpe_str, dep_cpe_str, version=None, release_date=None):
        return cve_scan.cpe_match(
            self.build_cpe(cpe_str),
            self.build_dep(dep_cpe_str, version=version, release_date=release_date))

    def test_cpe_match(self):
        # Mismatched part
        self.assertFalse(self.cpe_match('cpe:2.3:o:foo:bar:*', 'cpe:2.3:a:foo:bar:*'))
        # Mismatched vendor
        self.assertFalse(self.cpe_match('cpe:2.3:a:foo:bar:*', 'cpe:2.3:a:foz:bar:*'))
        # Mismatched product
        self.assertFalse(self.cpe_match('cpe:2.3:a:foo:bar:*', 'cpe:2.3:a:foo:baz:*'))
        # Wildcard product
        self.assertTrue(self.cpe_match('cpe:2.3:a:foo:bar:*', 'cpe:2.3:a:foo:*:*'))
        # Wildcard version match
        self.assertTrue(self.cpe_match('cpe:2.3:a:foo:bar:*', 'cpe:2.3:a:foo:bar:*'))
        # Exact version match
        self.assertTrue(
            self.cpe_match('cpe:2.3:a:foo:bar:1.2.3', 'cpe:2.3:a:foo:bar:*', version='1.2.3'))
        # Date version match
        self.assertTrue(
            self.cpe_match(
                'cpe:2.3:a:foo:bar:2020-03-05', 'cpe:2.3:a:foo:bar:*', release_date='2020-03-05'))
        fuzzy_version_matches = [
            ('2020-03-05', '2020-03-05'),
            ('2020-03-05', '20200305'),
            ('2020-03-05', 'foo-20200305-bar'),
            ('2020-03-05', 'foo-2020_03_05-bar'),
            ('2020-03-05', 'foo-2020-03-05-bar'),
            ('1.2.3', '1.2.3'),
            ('1.2.3', '1-2-3'),
            ('1.2.3', '1_2_3'),
            ('1.2.3', '1:2:3'),
            ('1.2.3', 'foo-1-2-3-bar'),
        ]
        for cpe_version, dep_version in fuzzy_version_matches:
            self.assertTrue(
                self.cpe_match(
                    f'cpe:2.3:a:foo:bar:{cpe_version}', 'cpe:2.3:a:foo:bar:*', version=dep_version))
        fuzzy_version_no_matches = [
            ('2020-03-05', '2020-3.5'),
            ('2020-03-05', '2020--03-05'),
            ('1.2.3', '1@2@3'),
            ('1.2.3', '1..2.3'),
        ]
        for cpe_version, dep_version in fuzzy_version_no_matches:
            self.assertFalse(
                self.cpe_match(
                    f'cpe:2.3:a:foo:bar:{cpe_version}', 'cpe:2.3:a:foo:bar:*', version=dep_version))

    def build_cve(self, cve_id, cpes, published_date):
        return cve_scan.Cve(
            cve_id,
            description=None,
            cpes=cpes,
            score=None,
            severity=None,
            published_date=dt.date.fromisoformat(published_date),
            last_modified_date=None)

    def cve_match(self, cve_id, cpes, published_date, dep_cpe_str, version=None, release_date=None):
        return cve_scan.cve_match(
            self.build_cve(cve_id, cpes=cpes, published_date=published_date),
            self.build_dep(dep_cpe_str, version=version, release_date=release_date))

    def test_cve_match(self):
        # Empty CPEs, no match
        self.assertFalse(self.cve_match('CVE-2020-123', set(), '2020-05-03', 'cpe:2.3:a:foo:bar:*'))
        # Wildcard version, stale dependency match
        self.assertTrue(
            self.cve_match(
                'CVE-2020-123',
                set([self.build_cpe('cpe:2.3:a:foo:bar:*')]),
                '2020-05-03',
                'cpe:2.3:a:foo:bar:*',
                release_date='2020-05-02'))
        self.assertTrue(
            self.cve_match(
                'CVE-2020-123',
                set([self.build_cpe('cpe:2.3:a:foo:bar:*')]),
                '2020-05-03',
                'cpe:2.3:a:foo:bar:*',
                release_date='2020-05-03'))
        # Wildcard version, recently updated
        self.assertFalse(
            self.cve_match(
                'CVE-2020-123',
                set([self.build_cpe('cpe:2.3:a:foo:bar:*')]),
                '2020-05-03',
                'cpe:2.3:a:foo:bar:*',
                release_date='2020-05-04'))
        # Version match
        self.assertTrue(
            self.cve_match(
                'CVE-2020-123',
                set([self.build_cpe('cpe:2.3:a:foo:bar:1.2.3')]),
                '2020-05-03',
                'cpe:2.3:a:foo:bar:*',
                version='1.2.3'))
        # Version mismatch
        self.assertFalse(
            self.cve_match(
                'CVE-2020-123',
                set([self.build_cpe('cpe:2.3:a:foo:bar:1.2.3')]),
                '2020-05-03',
                'cpe:2.3:a:foo:bar:*',
                version='1.2.4',
                release_date='2020-05-02'))
        # Multiple CPEs, match first, don't match later.
        self.assertTrue(
            self.cve_match(
                'CVE-2020-123',
                set([
                    self.build_cpe('cpe:2.3:a:foo:bar:1.2.3'),
                    self.build_cpe('cpe:2.3:a:foo:baz:3.2.1')
                ]),
                '2020-05-03',
                'cpe:2.3:a:foo:bar:*',
                version='1.2.3'))

    def test_cve_scan(self):
        cves = {
            'CVE-2020-1234':
                self.build_cve(
                    'CVE-2020-1234',
                    set([
                        self.build_cpe('cpe:2.3:a:foo:bar:1.2.3'),
                        self.build_cpe('cpe:2.3:a:foo:baz:3.2.1')
                    ]), '2020-05-03'),
            'CVE-2020-1235':
                self.build_cve(
                    'CVE-2020-1235',
                    set([
                        self.build_cpe('cpe:2.3:a:foo:bar:1.2.3'),
                        self.build_cpe('cpe:2.3:a:foo:baz:3.2.1')
                    ]), '2020-05-03'),
            'CVE-2020-1236':
                self.build_cve(
                    'CVE-2020-1236', set([
                        self.build_cpe('cpe:2.3:a:foo:wat:1.2.3'),
                    ]), '2020-05-03'),
        }
        cpe_revmap = {
            'cpe:2.3:a:foo:*:*': ['CVE-2020-1234', 'CVE-2020-1235', 'CVE-2020-1236'],
        }
        cve_allowlist = ['CVE-2020-1235']
        repository_locations = {
            'bar': self.build_dep('cpe:2.3:a:foo:bar:*', version='1.2.3'),
            'baz': self.build_dep('cpe:2.3:a:foo:baz:*', version='3.2.1'),
            'foo': self.build_dep('cpe:2.3:a:foo:*:*', version='1.2.3'),
            'blah': self.build_dep('N/A'),
        }
        possible_cves, cve_deps = cve_scan.cve_scan(
            cves, cpe_revmap, cve_allowlist, repository_locations)
        self.assertListEqual(sorted(possible_cves.keys()), ['CVE-2020-1234', 'CVE-2020-1236'])
        self.assertDictEqual(
            cve_deps, {
                'CVE-2020-1234': ['bar', 'baz', 'foo'],
                'CVE-2020-1236': ['foo']
            })


if __name__ == '__main__':
    unittest.main()
