package io.envoyproxy.envoymobile.shared

import android.support.v7.widget.RecyclerView
import android.util.Log
import android.view.View
import android.widget.TextView

class ResponseViewHolder(itemView: View) : RecyclerView.ViewHolder(itemView) {
  private val countTextView: TextView = itemView.findViewById(R.id.response_text_view_count) as TextView
  private val responseTextView: TextView = itemView.findViewById(R.id.response_text_view) as TextView
  private val headerTextView: TextView = itemView.findViewById(R.id.header_text_view) as TextView

  fun setResult(count: Int, response: Response) {
    countTextView.text = count.toString()
    response.fold(
        { success ->
          responseTextView.text = responseTextView.resources.getString(R.string.title_string, success.title)
          headerTextView.text = headerTextView.resources.getString(R.string.header_string, success.header)
          Log.d("ResponseViewHolder", "successful response!")
        },
        { failure ->
          responseTextView.text = responseTextView.resources.getString(R.string.title_string, failure.message)
          headerTextView.visibility = View.GONE
          itemView.setBackgroundResource(R.color.failed_color)
        })
  }
}
