// Copyright 2015 Google Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Application that finds all Xcodes installed on a given Mac and will return a path
// for a given version number.
// If you have 7.0, 6.4.1 and 6.3 installed the inputs will map to:
// 7,7.0,7.0.0 = 7.0
// 6,6.4,6.4.1 = 6.4.1
// 6.3,6.3.0 = 6.3

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

#import <CoreServices/CoreServices.h>
#import <Foundation/Foundation.h>

// Simple data structure for tracking a version of Xcode (i.e. 6.4) with an URL to the
// appplication.
@interface XcodeVersionEntry : NSObject
@property(readonly) NSString *version;
@property(readonly) NSURL *url;
@end

@implementation XcodeVersionEntry

- (id)initWithVersion:(NSString *)version url:(NSURL *)url {
  if ((self = [super init])) {
    _version = version;
    _url = url;
  }
  return self;
}

- (id)description {
  return [NSString stringWithFormat:@"<%@ %p>: %@ %@", [self class], self, _version, _url];
}

@end

// Given an entry, insert it into a dictionary that is keyed by versions.
// For an entry that is 6.4.1:/Applications/Xcode.app
// Add it for 6.4.1, and optionally add it for 6.4 and 6 if it is "better" than any entry that may
// already be there, where "better" is defined as:
// 1. Under /Applications/. (This avoids mounted xcode versions taking precedence over installed
//    versions.)
// 2. Not older (at least as high version number).
static void AddEntryToDictionary(XcodeVersionEntry *entry, NSMutableDictionary *dict) {
  BOOL inApplications = [entry.url.path rangeOfString:@"/Applications/"].location != NSNotFound;
  NSString *entryVersion = entry.version;
  NSString *subversion = entryVersion;
  if (dict[entryVersion] && !inApplications) {
    return;
  }
  dict[entryVersion] = entry;
  while (YES) {
    NSRange range = [subversion rangeOfString:@"." options:NSBackwardsSearch];
    if (range.length == 0 || range.location == 0) {
      break;
    }
    subversion = [subversion substringToIndex:range.location];
    XcodeVersionEntry *subversionEntry = dict[subversion];
    if (subversionEntry) {
      BOOL atLeastAsLarge =
          ([subversionEntry.version compare:entry.version] == NSOrderedDescending);
      if (inApplications && atLeastAsLarge) {
        dict[subversion] = entry;
      }
    } else {
      dict[subversion] = entry;
    }
  }
}

// Given a "version", expand it to at least 3 components by adding .0 as necessary.
static NSString *ExpandVersion(NSString *version) {
  NSArray *components = [version componentsSeparatedByString:@"."];
  NSString *appendage = nil;
  if (components.count == 2) {
    appendage = @".0";
  } else if (components.count == 1) {
    appendage = @".0.0";
  }
  if (appendage) {
    version = [version stringByAppendingString:appendage];
  }
  return version;
}

int main(int argc, const char * argv[]) {
  @autoreleasepool {
    NSString *versionArg = nil;
    BOOL versionsOnly = NO;
    if (argc == 1) {
      versionArg = @"";
    } else if (argc == 2) {
      NSString *firstArg = [NSString stringWithUTF8String:argv[1]];
      if ([@"-v" isEqualToString:firstArg]) {
        versionsOnly = YES;
        versionArg = @"";
      } else {
        versionArg = firstArg;
        NSCharacterSet *versSet =
            [NSCharacterSet characterSetWithCharactersInString:@"0123456789."];
        if ([versionArg rangeOfCharacterFromSet:versSet.invertedSet].length != 0) {
          versionArg = nil;
        }
      }
    }
    if (versionArg == nil) {
      printf("xcode_locator [-v|<version_number>]\n"
             "Given a version number, or partial version number in x.y.z format, will attempt "
             "to return the path to the appropriate developer directory.\nOmitting a version "
             "number will list all available versions in JSON format, alongside their paths.\n"
             "Passing -v will list all available fully-specified version numbers along with "
             "their possible aliases and their developer directory, each on a new line.\n"
             "For example: '7.3.1:7,7.3,7.3.1:/Applications/Xcode.app/Contents/Developer'.\n");
      return 1;
    }

    NSMutableDictionary *dict = [[NSMutableDictionary alloc] init];
    CFErrorRef cfError;
    NSArray *array = CFBridgingRelease(LSCopyApplicationURLsForBundleIdentifier(
        CFSTR("com.apple.dt.Xcode"), &cfError));
    if (array == nil) {
      NSError *nsError = (__bridge NSError *)cfError;
      printf("error: %s\n", nsError.description.UTF8String);
      return 1;
    }
    for (NSURL *url in array) {
      NSBundle *bundle = [NSBundle bundleWithURL:url];
      if (!bundle) {
        printf("error: Unable to open bundle at URL: %s\n", url.description.UTF8String);
        return 1;
      }
      NSString *version = bundle.infoDictionary[@"CFBundleShortVersionString"];
      if (!version) {
        printf("error: Unable to extract CFBundleShortVersionString from URL: %s\n",
               url.description.UTF8String);
        return 1;
      }
      version = ExpandVersion(version);
      NSURL *developerDir = [url URLByAppendingPathComponent:@"Contents/Developer"];
      XcodeVersionEntry *entry =
          [[XcodeVersionEntry alloc] initWithVersion:version url:developerDir];
      AddEntryToDictionary(entry, dict);
    }

    XcodeVersionEntry *entry = [dict objectForKey:versionArg];
    if (entry) {
      printf("%s\n", entry.url.fileSystemRepresentation);
      return 0;
    }

    if (versionsOnly) {
      NSSet *distinctValues = [[NSSet alloc] initWithArray:[dict allValues]];
      NSMutableDictionary *aliasDict = [[NSMutableDictionary alloc] init];
      for (XcodeVersionEntry *value in distinctValues) {
        NSString *versionString = value.version;
        if (aliasDict[versionString] == nil) {
          aliasDict[versionString] = [[NSMutableSet alloc] init];
        }
        [aliasDict[versionString] addObjectsFromArray:[dict allKeysForObject:value]];
      }
      for (NSString *version in aliasDict) {
        XcodeVersionEntry *entry = dict[version];
        printf("%s:%s:%s\n",
               version.UTF8String,
               [[aliasDict[version] allObjects] componentsJoinedByString: @","].UTF8String,
               entry.url.fileSystemRepresentation);
      }
    } else {
      // Print out list in json format.
      printf("{\n");
      for (NSString *version in dict) {
        XcodeVersionEntry *entry = dict[version];
        printf("\t\"%s\": \"%s\",\n", version.UTF8String, entry.url.fileSystemRepresentation);
      }
      printf("}\n");
    }
    return ([@"" isEqualToString:versionArg] ? 0 : 1);
  }
}
