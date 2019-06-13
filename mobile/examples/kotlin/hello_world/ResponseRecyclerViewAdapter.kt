package io.envoyproxy.envoymobile.helloenvoykotlin

import android.support.v7.widget.RecyclerView
import android.view.LayoutInflater
import android.view.ViewGroup

class ResponseRecyclerViewAdapter : RecyclerView.Adapter<ResponseViewHolder>() {
  private val data = mutableListOf<Response>()

  override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): ResponseViewHolder {
    val context = parent.context
    val inflater = LayoutInflater.from(context)
    val view = inflater.inflate(R.layout.item, parent, false)
    return ResponseViewHolder(view)
  }

  override fun onBindViewHolder(holder: ResponseViewHolder, position: Int) {
    holder.setResult(data[position])
  }

  override fun getItemCount(): Int {
    return data.size
  }

  fun add(response: Response) {
    data.add(0, response)
    notifyItemInserted(0)
  }
}
