package io.envoyproxy.envoymobile.helloenvoy;

import android.content.Context;
import android.support.v7.widget.RecyclerView;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;

import java.util.ArrayList;
import java.util.List;

public class ResponseRecyclerViewAdapter extends RecyclerView.Adapter<ResponseViewHolder> {
    private final List<Response> data = new ArrayList<>();

    @Override
    public ResponseViewHolder onCreateViewHolder(ViewGroup parent, int viewType) {
        Context context = parent.getContext();
        LayoutInflater inflater = LayoutInflater.from(context);
        View view = inflater.inflate(R.layout.item, parent, false);
        return new ResponseViewHolder(view);
    }

    @Override
    public void onBindViewHolder(ResponseViewHolder holder, int position) {
        holder.setResult(data.get(position));
    }

    @Override
    public int getItemCount() {
        return data.size();
    }

    public void add(Response response) {
        data.add(0, response);
        notifyItemInserted(0);
    }
}
