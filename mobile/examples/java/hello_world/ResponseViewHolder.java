package io.envoyproxy.envoymobile.helloenvoy;

import android.support.v7.widget.RecyclerView;
import android.view.View;
import android.widget.TextView;

public class ResponseViewHolder extends RecyclerView.ViewHolder {
  private final TextView responseTextView;
  private final TextView headerTextView;

  public ResponseViewHolder(View itemView) {
    super(itemView);
    this.responseTextView = (TextView)itemView.findViewById(R.id.response_text_view);
    this.headerTextView = (TextView)itemView.findViewById(R.id.header_text_view);
  }

  public void setResult(Response response) {
    responseTextView.setText(
        responseTextView.getResources().getString(R.string.title_string, response.title));
    headerTextView.setText(
        headerTextView.getResources().getString(R.string.header_string, response.header));
  }
}
