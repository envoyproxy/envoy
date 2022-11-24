import NetworkClient
import SwiftUI

struct ContentView: View {
    @State private var text = "Loading..."
    @State private var isLoading = true
    @State private var isError = false

    var body: some View {
        Button("Get cat fact") {
            Task { await getFact() }
        }

        Text(text)
            .foregroundColor(isError ? .red : nil)
            .redacted(reason: isLoading ? .placeholder : [])
            .padding()
            .task { await getFact() }
    }

    private func getFact() async {
        isLoading = true
        isError = false

        do {
            text = try await NetworkClient.getCatFact()
            print("Successfully got cat fact:", text)
        } catch {
            isError = true
            text = error.localizedDescription
            print("Failed to get cat fact:", text)
        }

        isLoading = false
    }
}
