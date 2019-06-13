package io.envoyproxy.envoymobile.helloenvoykotlin

import android.support.v7.widget.RecyclerView
import android.view.View
import android.widget.TextView

class ResponseViewHolder(itemView: View) : RecyclerView.ViewHolder(itemView) {
  private val responseTextView: TextView = itemView.findViewById(R.id.response_text_view) as TextView
  private val headerTextView: TextView = itemView.findViewById(R.id.header_text_view) as TextView

  fun setResult(response: Response) {
    responseTextView.text = responseTextView.resources.getString(R.string.title_string, response.title)
    headerTextView.text = headerTextView.resources.getString(R.string.header_string, response.header)
  }
}
